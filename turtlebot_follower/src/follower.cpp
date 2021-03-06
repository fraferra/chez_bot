/*
 * Copyright (c) 2011, Willow Garage, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Willow Garage, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <ros/ros.h>
#include <pluginlib/class_list_macros.h>
#include <nodelet/nodelet.h>
#include <geometry_msgs/Twist.h>
#include <sensor_msgs/Image.h>
#include <visualization_msgs/Marker.h>
#include <turtlebot_msgs/SetFollowState.h>
#include <cmvision/Blob.h>
#include <cmvision/Blobs.h>
#include "dynamic_reconfigure/server.h"
#include "turtlebot_follower/FollowerConfig.h"
//#include <turtlesim/Velocity.h>
#include "hog_haar_person_detection/Faces.h"
#include "hog_haar_person_detection/BoundingBox.h"
#include <depth_image_proc/depth_traits.h>

#include "keyboard/Key.h"


// Navigation Headers
#include "sensor_msgs/LaserScan.h"
#include "nav_msgs/Odometry.h"
#include "tf/tf.h"
#include <tf/transform_listener.h>
#include <fstream>

using namespace std;
#define LINEAR_VELOCITY_MINIMUM_THRESHOLD 0.2
#define ANGULAR_VELOCITY_MINIMUM_THRESHOLD 0.4

namespace turtlebot_follower
{

//* The turtlebot follower nodelet.
/**
 * The turtlebot follower nodelet. Subscribes to point clouds
 * from the 3dsensor, processes them, and publishes command vel
 * messages.
 */
class TurtlebotFollower : public nodelet::Nodelet
{
public:
  /*!
   * @brief The constructor for the follower.
   * Constructor for the follower.
   */
  TurtlebotFollower() : min_y_(0.1), max_y_(0.5),
                        min_x_(-0.2), max_x_(0.2),
                        max_z_(0.8), goal_z_(0.6),
                        z_scale_(1.0), x_scale_(5.0)
  {

  }

  ~TurtlebotFollower()
  {
    delete config_srv_;
  }

private:
  double min_y_; /**< The minimum y position of the points in the box. */
  double max_y_; /**< The maximum y position of the points in the box. */
  double min_x_; /**< The minimum x position of the points in the box. */
  double max_x_; /**< The maximum x position of the points in the box. */
  double max_z_; /**< The maximum z position of the points in the box. */
  double goal_z_; /**< The distance away from the robot to hold the centroid */
  double z_scale_; /**< The scaling factor for translational robot speed */
  double x_scale_; /**< The scaling factor for rotational robot speed */
  bool   enabled_; /**< Enable/disable following; just prevents motor commands */
  bool color_found;
  bool face_found;
  float x_face;
  float y_face;
  float x_yellow;
  float y_yellow;

  //declare publishers
  ros::Publisher velocityPublisher;
  //declare subscribers
  ros::Subscriber scanSubscriber;
  ros::Subscriber pose_subscriber;
  //global variable to update the position of the robot
  nav_msgs::Odometry turtlebot_odom_pose;
  //color_found = false;
  // Service for start/stop following
  ros::ServiceServer switch_srv_;

  // Dynamic reconfigure server
  dynamic_reconfigure::Server<turtlebot_follower::FollowerConfig>* config_srv_;

void MyposeCallback(const nav_msgs::Odometry::ConstPtr & pose_message){
	turtlebot_odom_pose.pose.pose.position.x=pose_message->pose.pose.position.x;
	turtlebot_odom_pose.pose.pose.position.y=pose_message->pose.pose.position.y;
	turtlebot_odom_pose.pose.pose.position.z=pose_message->pose.pose.position.z;

	turtlebot_odom_pose.pose.pose.orientation.w=pose_message->pose.pose.orientation.w;
	turtlebot_odom_pose.pose.pose.orientation.x=pose_message->pose.pose.orientation.x;
	turtlebot_odom_pose.pose.pose.orientation.y=pose_message->pose.pose.orientation.y;
	turtlebot_odom_pose.pose.pose.orientation.z=pose_message->pose.pose.orientation.z;
}

/**
 * a function that makes the robot move straight
 * @param speed: represents the speed of the robot the robot
 * @param distance: represents the distance to move by the robot
 * @param isForward: if true, the robot moves forward,otherwise, it moves backward
 *
 * Method 1: using tf and Calculate the distance between the two transformations
 */
void move_v1(double speed, double distance, bool isForward){
	//declare a Twist message to send velocity commands
	geometry_msgs::Twist VelocityMessage;
	//declare tf transform listener: this transform listener will be used to listen and capture the transformation between
	// the /odom frame (that represent the reference frame) and the base_footprint frame the represent moving frame
	tf::TransformListener listener;
	//declare tf transform
	//init_transform: is the transformation before starting the motion
	tf::StampedTransform init_transform;
	//current_transformation: is the transformation while the robot is moving
	tf::StampedTransform current_transform;


	//set the linear velocity to a positive value if isFoward is true
	if (isForward)
		VelocityMessage.linear.x =abs(speed);
	else //else set the velocity to negative value to move backward
		VelocityMessage.linear.x =-abs(speed);
	//all velocities of other axes must be zero.
	VelocityMessage.linear.y =0;
	VelocityMessage.linear.z =0;
	//The angular velocity of all axes must be zero because we want  a straight motion
	VelocityMessage.angular.x = 0;
	VelocityMessage.angular.y = 0;
	VelocityMessage.angular.z =0;

	double distance_moved = 0.0;
	ros::Rate loop_rate(10); // we publish the velocity at 10 Hz (10 times a second)

	/*
	 * First, we capture the initial transformation before starting the motion.
	 * we call this transformation "init_transform"
	 * It is important to "waitForTransform" otherwise, it might not be captured.
	 */
	try{
		//wait for the transform to be found
		listener.waitForTransform("/base_footprint", "/odom", ros::Time(0), ros::Duration(10.0) );
		//Once the transform is found,get the initial_transform transformation.
		listener.lookupTransform("/base_footprint", "/odom",ros::Time(0), init_transform);
	}
	catch (tf::TransformException & ex){
		ROS_ERROR(" Problem %s",ex.what());
		ros::Duration(1.0).sleep();
	}



	do{
		/***************************************
		 * STEP1. PUBLISH THE VELOCITY MESSAGE
		 ***************************************/
		velocityPublisher.publish(VelocityMessage);
		ros::spinOnce();
		loop_rate.sleep();
		/**************************************************
		 * STEP2. ESTIMATE THE DISTANCE MOVED BY THE ROBOT
		 *************************************************/
		try{
			//wait for the transform to be found
			listener.waitForTransform("/base_footprint", "/odom", ros::Time(0), ros::Duration(10.0) );
			//Once the transform is found,get the initial_transform transformation.
			listener.lookupTransform("/base_footprint", "/odom",ros::Time(0), current_transform);
		}
		catch (tf::TransformException & ex){
			ROS_ERROR(" Problem %s",ex.what());
			ros::Duration(1.0).sleep();
		}
		/*
		 * Calculate the distance moved by the robot
		 * There are two methods that give the same result
		 */

		/*
		 * Method 1: Calculate the distance between the two transformations
		 * Hint:
		 * 	  --> transform.getOrigin().x(): represents the x coordinate of the transformation
		 * 	  --> transform.getOrigin().y(): represents the y coordinate of the transformation
		 */
		//calculate the distance moved
		//cout<<"Initial Transform: "<<init_transform <<" , "<<"Current Transform: "<<current_transform<<endl;

		distance_moved = sqrt(pow((current_transform.getOrigin().x()-init_transform.getOrigin().x()), 2) +
				pow((current_transform.getOrigin().y()-init_transform.getOrigin().y()), 2));


	}while((distance_moved<distance)&&(ros::ok()));
	//finally, stop the robot when the distance is moved
	VelocityMessage.linear.x =0;
	velocityPublisher.publish(VelocityMessage);
}

double rotate(double angular_velocity, double radians,  bool clockwise)
{

	//delcare a Twist message to send velocity commands
	geometry_msgs::Twist VelocityMessage;
	//declare tf transform listener: this transform listener will be used to listen and capture the transformation between
	// the odom frame (that represent the reference frame) and the base_footprint frame the represent moving frame
	tf::TransformListener TFListener;
	//declare tf transform
	//init_transform: is the transformation before starting the motion
	tf::StampedTransform init_transform;
	//current_transformation: is the transformation while the robot is moving
	tf::StampedTransform current_transform;
	//initial coordinates (for method 3)
	nav_msgs::Odometry initial_turtlebot_odom_pose;

	double angle_turned =0.0;

	//validate angular velocity; ANGULAR_VELOCITY_MINIMUM_THRESHOLD is the minimum allowed
	angular_velocity=((angular_velocity>ANGULAR_VELOCITY_MINIMUM_THRESHOLD)?angular_velocity:ANGULAR_VELOCITY_MINIMUM_THRESHOLD);

	while(radians < 0) radians += 2*M_PI;
	while(radians > 2*M_PI) radians -= 2*M_PI;

	//wait for the listener to get the first message
	TFListener.waitForTransform("base_footprint", "odom", ros::Time(0), ros::Duration(1.0));


	//record the starting transform from the odometry to the base frame
	TFListener.lookupTransform("base_footprint", "odom", ros::Time(0), init_transform);


	//the command will be to turn at 0.75 rad/s
	VelocityMessage.linear.x = VelocityMessage.linear.y = 0.0;
	VelocityMessage.angular.z = angular_velocity;
	if (clockwise) VelocityMessage.angular.z = -VelocityMessage.angular.z;

	//the axis we want to be rotating by
	tf::Vector3 desired_turn_axis(0,0,1);
	if (!clockwise) desired_turn_axis = -desired_turn_axis;

	ros::Rate rate(10.0);
	bool done = false;
	while (!done )
	{
		//send the drive command
		velocityPublisher.publish(VelocityMessage);
		rate.sleep();
		//get the current transform
		try
		{
			TFListener.waitForTransform("base_footprint", "odom", ros::Time(0), ros::Duration(1.0));
			TFListener.lookupTransform("base_footprint", "odom", ros::Time(0), current_transform);
		}
		catch (tf::TransformException ex)
		{
			ROS_ERROR("%s",ex.what());
			break;
		}
		tf::Transform relative_transform = init_transform.inverse() * current_transform;
		tf::Vector3 actual_turn_axis = relative_transform.getRotation().getAxis();
		angle_turned = relative_transform.getRotation().getAngle();

		if (fabs(angle_turned) < 1.0e-2) continue;
		if (actual_turn_axis.dot(desired_turn_axis ) < 0 )
			angle_turned = 2 * M_PI - angle_turned;

		if (!clockwise)
			VelocityMessage.angular.z = (angular_velocity-ANGULAR_VELOCITY_MINIMUM_THRESHOLD) * (fabs(radian2degree(radians-angle_turned)/radian2degree(radians)))+ANGULAR_VELOCITY_MINIMUM_THRESHOLD;
		else
			if (clockwise)
				VelocityMessage.angular.z = (-angular_velocity+ANGULAR_VELOCITY_MINIMUM_THRESHOLD) * (fabs(radian2degree(radians-angle_turned)/radian2degree(radians)))-ANGULAR_VELOCITY_MINIMUM_THRESHOLD;

		if (angle_turned > radians) {
			done = true;
			VelocityMessage.linear.x = VelocityMessage.linear.y = VelocityMessage.angular.z = 0;
			velocityPublisher.publish(VelocityMessage);
		}


	}
	if (done) return angle_turned;
	return angle_turned;
}


double calculateYaw( double x1, double y1, double x2,double y2)
{

	double bearing = atan2((y2 - y1),(x2 - x1));
	//if(bearing < 0) bearing += 2 * PI;
	bearing *= 180.0 / M_PI;
	return bearing;
}

/* makes conversion from radian to degree */
double radian2degree(double radianAngle){
	return (radianAngle*57.2957795);
}


/* makes conversion from degree to radian */
double degree2radian(double degreeAngle){
	return (degreeAngle/57.2957795);
}


void personDetectionCallBack(const hog_haar_person_detection::Faces facelist)
{
	float tmp_x = 0.0;
	float tmp_y = 0.0;
	float count = 0;
	//ROS_INFO_THROTTLE(1, facelist);
	ROS_INFO_THROTTLE(1, "FACE CHECK\n");
	//ROS_INFO_THROTTLE(1, "%f\n", facelist.faces[0].center.x);
	
	//if(sizeof(facelist.faces) != 0){ 
          if(!facelist.faces.empty()){
		ROS_INFO_THROTTLE(1, "FACE FOUND\n");
		
	    //ROS_INFO_THROTTLE(1, "%d",sizeof(facelist.faces));
	    //ROS_INFO_THROTTLE(1, "%f",sizeof(facelist.faces)/sizeof(facelist.faces[0]));
	       x_face = facelist.faces[0].center.x;
	       y_face = facelist.faces[0].center.y;
	       y_yellow = ((facelist.faces[0].center.y - 320.0)/640.0 + y_yellow)/2.0;
	       x_yellow = ((facelist.faces[0].center.x - 320.0)/640.0 + x_yellow)/2.0;
	    //ROS_INFO_THROTTLE(1, "%f\n", x_face);
	       face_found = true;
		color_found = true;
	    int i = 0;
	    /*while(!facelist.faces.empty()){

			f = facelist.faces.front();
			facelist.faces.pop_front();
	    		tmp_x += f.center.x;
	    		tmp_y += f.center.y;
	    		count += 1.0;
			i++;
	     }
	    x_face = tmp_x/count;
	    y_face = tmp_x/count;*/
	 }else{
		ROS_INFO_THROTTLE(1, "FACE ->NOT<- FOUND\n");
		face_found = false;
		color_found = false;
	}
					
	//}
	//if(sizeof(facelist.faces) == 0){
	//	ROS_INFO_THROTTLE(1, "FACE ->NOT<- FOUND\n");
	//	face_found = false;
	//}
	//x_face = tmp_x / count;
	//y_face = tmp_y / count;
	
}


void keyboardCallback(const keyboard::Keyboard key){
          if(key.code == 32){
            ROS_INFO_THROTTLE(1, "KEY PRESSED\n");
          }

}

/*!
   * @brief OnInit method from node handle.
   * OnInit method from node handle. Sets up the parameters
   * and topics.
   */

  virtual void onInit()
  {
    ros::NodeHandle& nh = getNodeHandle();
    ros::NodeHandle& private_nh = getPrivateNodeHandle();

    private_nh.getParam("min_y", min_y_);
    private_nh.getParam("max_y", max_y_);
    private_nh.getParam("min_x", min_x_);
    private_nh.getParam("max_x", max_x_);
    private_nh.getParam("max_z", max_z_);
    private_nh.getParam("goal_z", goal_z_);
    private_nh.getParam("z_scale", z_scale_);
    private_nh.getParam("x_scale", x_scale_);
    private_nh.getParam("enabled", enabled_);

    cmdpub_ = private_nh.advertise<geometry_msgs::Twist> ("cmd_vel", 1);
    markerpub_ = private_nh.advertise<visualization_msgs::Marker>("marker",1);
    bboxpub_ = private_nh.advertise<visualization_msgs::Marker>("bbox",1);
    sub_= nh.subscribe<sensor_msgs::Image>("depth/image_rect", 1, &TurtlebotFollower::imagecb, this);
    //blobsSubscriber = nh.subscribe("/blobs", 100,  &TurtlebotFollower::blobsCallBack, this);
    facesSubscriber = nh.subscribe("/person_detection/faces", 100,  &TurtlebotFollower::personDetectionCallBack, this);
    switch_srv_ = private_nh.advertiseService("change_state", &TurtlebotFollower::changeModeSrvCb, this);

    keyboardSub = nh.subscribe("/keyboard/keydown", 100,  &TurtlebotFollower::keyboardCallback, this);

    config_srv_ = new dynamic_reconfigure::Server<turtlebot_follower::FollowerConfig>(private_nh);
    dynamic_reconfigure::Server<turtlebot_follower::FollowerConfig>::CallbackType f =
        boost::bind(&TurtlebotFollower::reconfigure, this, _1, _2);
    config_srv_->setCallback(f);

        //NAVIGATION
	//subscribe to the odometry topic to get the position of the robot
	pose_subscriber = nh.subscribe("/odom", 10, &TurtlebotFollower::MyposeCallback, this);
	//register the velocity publisher
	velocityPublisher =nh.advertise<geometry_msgs::Twist>("/cmd_vel_mux/input/navi", 1000);
	//velocityPublisher =n.advertise<geometry_msgs::Twist>("/cmd_vel", 1000);
	ros::spinOnce();
	ros::Rate loop(1);
	loop.sleep();loop.sleep();loop.sleep();//loop.sleep();loop.sleep();
	ros::spinOnce();	
    
  }

  void reconfigure(turtlebot_follower::FollowerConfig &config, uint32_t level)
  {
    min_y_ = config.min_y;
    max_y_ = config.max_y;
    min_x_ = config.min_x;
    max_x_ = config.max_x;
    max_z_ = config.max_z;
    goal_z_ = config.goal_z;
    z_scale_ = config.z_scale;
    x_scale_ = config.x_scale;
  }

  /*!
   * @brief Callback for point clouds.
   * Callback for depth images. It finds the centroid
   * of the points in a box in the center of the image. 
   * Publishes cmd_vel messages with the goal from the image.
   * @param cloud The point cloud message.
   */
  void imagecb(const sensor_msgs::ImageConstPtr& depth_msg)
  {

    // Precompute the sin function for each row and column
    uint32_t image_width = depth_msg->width;
    float x_radians_per_pixel = 60.0/57.0/image_width;
    float sin_pixel_x[image_width];
    for (int x = 0; x < image_width; ++x) {
      sin_pixel_x[x] = sin((x - image_width/ 2.0)  * x_radians_per_pixel);
    }

    uint32_t image_height = depth_msg->height;
    float y_radians_per_pixel = 45.0/57.0/image_width;
    float sin_pixel_y[image_height];
    for (int y = 0; y < image_height; ++y) {
      // Sign opposite x for y up values
      sin_pixel_y[y] = sin((image_height/ 2.0 - y)  * y_radians_per_pixel);
    }

    //X,Y,Z of the centroid
    float x = 0.0;
    float y = 0.0;
    float z = 1e6;
    //Number of points observed
    unsigned int n = 0;

    //Iterate through all the points in the region and find the average of the position
    const float* depth_row = reinterpret_cast<const float*>(&depth_msg->data[0]);
    int row_step = depth_msg->step / sizeof(float);
    for (int v = 0; v < (int)depth_msg->height; ++v, depth_row += row_step)
    {
     for (int u = 0; u < (int)depth_msg->width; ++u)
     {
       float depth = depth_image_proc::DepthTraits<float>::toMeters(depth_row[u]);
       if (!depth_image_proc::DepthTraits<float>::valid(depth) || depth > max_z_) continue;
       float y_val = sin_pixel_y[v] * depth;
       float x_val = sin_pixel_x[u] * depth;
       if ( y_val > min_y_ && y_val < max_y_ &&
            x_val > min_x_ && x_val < max_x_)
       {
         x += x_val;
         y += y_val;
         z = std::min(z, depth); //approximate depth as forward.
         n++;
       }
     }
    }

    //If there are points, find the centroid and calculate the command goal.
    //If there are no points, simply publish a stop goal.
    if (n < 4000 and color_found)
    {
      x = x_yellow;
      y = y_yellow;
      /*if(z > max_z_){
        ROS_INFO_THROTTLE(1, "Centroid too far away %f, stopping the robot\n", z);
        if (enabled_)
        {
          cmdpub_.publish(geometry_msgs::TwistPtr(new geometry_msgs::Twist()));
        }
        return;
      }*/

      
      publishMarker(x, y, z);

      if (enabled_)
      {
ROS_INFO_THROTTLE(1, "BLOB detected at Centroid at %f %f", x, y);
        geometry_msgs::TwistPtr cmd(new geometry_msgs::Twist());
        cmd->linear.x = 0.05;//(z - goal_z_) * z_scale_;
	cmd->angular.z = -x * z_scale_;
        cmdpub_.publish(cmd);
      }
    }
else if(n > 4000){
ROS_INFO_THROTTLE(1, "obstacle detected");
//for(int i=0;i<5;i++){
geometry_msgs::TwistPtr cmd1(new geometry_msgs::Twist());
//cmd1->angular.z = 1;
//cmdpub_.publish(cmd1);
//}
//for(int i=0;i<5;i++){
//geometry_msgs::TwistPtr cmd(new geometry_msgs::Twist());
geometry_msgs::TwistPtr cmd2(new geometry_msgs::Twist());
cmd2->linear.x = -2.5;
cmdpub_.publish(cmd2);

geometry_msgs::TwistPtr cmd3(new geometry_msgs::Twist());
//cmd3->angular.z = -1;
//cmdpub_.publish(cmd3);
//}
/*for(int i=0;i<5;i++){
geometry_msgs::TwistPtr cmd(new geometry_msgs::Twist());
cmd->angular.z = -0.1;
cmdpub_.publish(cmd);
}*/
ROS_INFO_THROTTLE(1, "obstacle bypassed");
}
else if(!color_found){
ROS_INFO_THROTTLE(1, "no color blob found, searching...");
	/*geometry_msgs::TwistPtr cmd(new geometry_msgs::Twist());
	    cmd->angular.z = 1;
        cmdpub_.publish(cmd);*/
}
    else
    {
      ROS_INFO_THROTTLE(1, "Not enough points(%d) detected, stopping the robot", n);
      publishMarker(x, y, z);

      if (enabled_)
      {
        cmdpub_.publish(geometry_msgs::TwistPtr(new geometry_msgs::Twist()));
      }
    }

    publishBbox();
  }

  bool changeModeSrvCb(turtlebot_msgs::SetFollowState::Request& request,
                       turtlebot_msgs::SetFollowState::Response& response)
  {
    if ((enabled_ == true) && (request.state == request.STOPPED))
    {
      ROS_INFO("Change mode service request: following stopped");
      cmdpub_.publish(geometry_msgs::TwistPtr(new geometry_msgs::Twist()));
      enabled_ = false;
    }
    else if ((enabled_ == false) && (request.state == request.FOLLOW))
    {
      ROS_INFO("Change mode service request: following (re)started");
      enabled_ = true;
    }

    response.result = response.OK;
    return true;
  }

  void publishMarker(double x,double y,double z)
  {
    visualization_msgs::Marker marker;
    marker.header.frame_id = "/camera_rgb_optical_frame";
    marker.header.stamp = ros::Time();
    marker.ns = "my_namespace";
    marker.id = 0;
    marker.type = visualization_msgs::Marker::SPHERE;
    marker.action = visualization_msgs::Marker::ADD;
    marker.pose.position.x = x;
    marker.pose.position.y = y;
    marker.pose.position.z = z;
    marker.pose.orientation.x = 0.0;
    marker.pose.orientation.y = 0.0;
    marker.pose.orientation.z = 0.0;
    marker.pose.orientation.w = 1.0;
    marker.scale.x = 0.2;
    marker.scale.y = 0.2;
    marker.scale.z = 0.2;
    marker.color.a = 1.0;
    marker.color.r = 1.0;
    marker.color.g = 0.0;
    marker.color.b = 0.0;
    //only if using a MESH_RESOURCE marker type:
    markerpub_.publish( marker );
  }

  void publishBbox()
  {
    double x = (min_x_ + max_x_)/2;
    double y = (min_y_ + max_y_)/2;
    double z = (0 + max_z_)/2;

    double scale_x = (max_x_ - x)*2;
    double scale_y = (max_y_ - y)*2;
    double scale_z = (max_z_ - z)*2;

    visualization_msgs::Marker marker;
    marker.header.frame_id = "/camera_rgb_optical_frame";
    marker.header.stamp = ros::Time();
    marker.ns = "my_namespace";
    marker.id = 1;
    marker.type = visualization_msgs::Marker::CUBE;
    marker.action = visualization_msgs::Marker::ADD;
    marker.pose.position.x = x;
    marker.pose.position.y = -y;
    marker.pose.position.z = z;
    marker.pose.orientation.x = 0.0;
    marker.pose.orientation.y = 0.0;
    marker.pose.orientation.z = 0.0;
    marker.pose.orientation.w = 1.0;
    marker.scale.x = scale_x;
    marker.scale.y = scale_y;
    marker.scale.z = scale_z;
    marker.color.a = 0.5;
    marker.color.r = 0.0;
    marker.color.g = 1.0;
    marker.color.b = 0.0;
    //only if using a MESH_RESOURCE marker type:
    bboxpub_.publish( marker );
  }

  ros::Subscriber sub_;
  ros::Publisher cmdpub_;
  ros::Publisher markerpub_;
  ros::Publisher bboxpub_;
  ros::Subscriber blobsSubscriber;
  ros::Subscriber facesSubscriber;
  ros::Subscriber keyboardSub;
};

PLUGINLIB_DECLARE_CLASS(turtlebot_follower, TurtlebotFollower, turtlebot_follower::TurtlebotFollower, nodelet::Nodelet);

}
