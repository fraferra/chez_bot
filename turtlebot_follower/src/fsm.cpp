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
  

  bool face_found;
  float x_face;
  float y_face;


  int STATE;

  float obstacle_detected;
  float is_close_to_human;
  float has_candies;
  //color_found = false;
  // Service for start/stop following
  ros::ServiceServer switch_srv_;

  // Dynamic reconfigure server
  dynamic_reconfigure::Server<turtlebot_follower::FollowerConfig>* config_srv_;

  /*!
   * @brief OnInit method from node handle.
   * OnInit method from node handle. Sets up the parameters
   * and topics.
   */

 // UPDATE STATE 
void updateState(){
  if(face_found == false && obstacle_detected == false && is_close_to_human==false){
    STATE = 0;
    TurtlebotFollower::searchMode();
  }

  else if(obstacle_detected == true && is_close_to_human==false){
    STATE = 1;
    TurtlebotFollower::avoidObstacle();
  }

  else if(face_found == true && obstacle_detected == false && is_close_to_human==false){
    STATE = 2;
    TurtlebotFollower::moveToHuman();
  }

  else if(face_found == true && is_close_to_human==true){
    STATE = 3;
    TurtlebotFollower::engageWithHuman();
  }

  else{STATE=0; TurtlebotFollower::searchMode();}

  ROS_INFO_THROTTLE(1, "STATE IS: %d\n", STATE);


}

void searchMode(){
  geometry_msgs::TwistPtr cmd2(new geometry_msgs::Twist());
  cmd2->linear.x = 0.3;
  cmdpub_.publish(cmd2);
}

void engageWithHuman(){
  system("espeak -v en 'HI, I AM CHEZ BOT. HOW ARE YOU?'");
  //system("espeak -v en 'WOULD YOU LIKE A CANDY? IF SO PRESS MY SPACEBAR'");
};


void avoidObstacle(){
  geometry_msgs::TwistPtr cmd2(new geometry_msgs::Twist());
  cmd2->linear.x = -1.0;
  cmdpub_.publish(cmd2);
};

void moveToHuman(){
        ROS_INFO_THROTTLE("GO TO HUMAN\n");
        geometry_msgs::TwistPtr cmd(new geometry_msgs::Twist());
        cmd->linear.x = 0.2;//(z - goal_z_) * z_scale_;
        cmd->angular.z = -x_face * z_scale_;
        cmdpub_.publish(cmd);
};

// UPDATE FACE DETECTION
void personDetectionCallBack(const hog_haar_person_detection::Faces facelist)
{
  float tmp_x = 0.0;
  float tmp_y = 0.0;
  float count = 0;
  //ROS_INFO_THROTTLE(1, facelist);
  //ROS_INFO_THROTTLE(1, "FACE CHECK\n");
  //ROS_INFO_THROTTLE(1, "%f\n", facelist.faces[0].center.x);
  
  //if(sizeof(facelist.faces) != 0){ 
          if(!facelist.faces.empty()){
    ROS_INFO_THROTTLE(1, "FACE FOUND\n");
    

         y_face = ((facelist.faces[0].center.y - 320.0)/640.0 + y_face)/2.0;
         x_face = ((facelist.faces[0].center.x - 320.0)/640.0 + x_face)/2.0;
      //ROS_INFO_THROTTLE(1, "%f\n", x_face);
         face_found = true;

         if(facelist.faces[0].width >100){
          is_close_to_human = true;
         }else{is_close_to_human = false;}
      int i = 0;

   }else{
    ROS_INFO_THROTTLE(1, "FACE ->NOT<- FOUND\n");
    face_found = false;
    is_close_to_human=false;
  }
          
TurtlebotFollower::updateState();
  
}


// UPDATE OBSTACLE DETECTION

  void updateObstacle(const sensor_msgs::ImageConstPtr& depth_msg)
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
    if(n>4000){obstacle_detected = true;
               ROS_INFO_THROTTLE(1, "OBSTACLE DETECTED\n");
              }else{obstacle_detected=false;
                 ROS_INFO_THROTTLE(1, "OBSTACLE NOT DETECTED\n");
              }
  }

void keyboardCallback(const keyboard::Key key){
          if(key.code == 32){
            ROS_INFO_THROTTLE(1, "KEY PRESSED\n");
          }
  }



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

    sub_= nh.subscribe<sensor_msgs::Image>("depth/image_rect", 1, &TurtlebotFollower::updateObstacle, this);

    facesSubscriber = nh.subscribe("/person_detection/faces", 100,  &TurtlebotFollower::personDetectionCallBack, this);

    keyboardSub = nh.subscribe("/keyboard/keydown", 100,  &TurtlebotFollower::keyboardCallback, this);

    //stateSub = nh.subscribe("/person_detection/faces", 100,  &TurtlebotFollower::updateState, this);



    config_srv_ = new dynamic_reconfigure::Server<turtlebot_follower::FollowerConfig>(private_nh);
    dynamic_reconfigure::Server<turtlebot_follower::FollowerConfig>::CallbackType f =
        boost::bind(&TurtlebotFollower::reconfigure, this, _1, _2);
    config_srv_->setCallback(f);



    

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


  ros::Subscriber sub_;
  ros::Publisher cmdpub_;
  ros::Publisher markerpub_;
  ros::Publisher bboxpub_;
  ros::Subscriber blobsSubscriber;
  ros::Subscriber facesSubscriber;
  ros::Subscriber keyboardSub;
  ros::Subscriber stateSub;
};

PLUGINLIB_DECLARE_CLASS(turtlebot_follower, TurtlebotFollower, turtlebot_follower::TurtlebotFollower, nodelet::Nodelet);

}
