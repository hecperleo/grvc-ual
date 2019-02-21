//----------------------------------------------------------------------------------------------------------------------
// GRVC UAL
//----------------------------------------------------------------------------------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2016 GRVC University of Seville
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
// Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS
// OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//----------------------------------------------------------------------------------------------------------------------

// #include <string>
// #include <chrono>
#include <uav_abstraction_layer/backend_dji.h>
// #include <Eigen/Eigen>
#include <ros/ros.h>
// #include <cmath>
#include <dji_sdk/dji_sdk.h>

#include <dji_sdk/Activation.h>
#include <dji_sdk/SetLocalPosRef.h>
#include <dji_sdk/SDKControlAuthority.h>
#include <dji_sdk/DroneTaskControl.h>
#include <dji_sdk/DroneArmControl.h>
// #include <std_msgs/UInt8.h>
// #include <std_msgs/Float64.h>
#include <sensor_msgs/Joy.h>
// #include <ros/package.h>
// #include <tf2_ros/transform_listener.h>
// #include <tf2_geometry_msgs/tf2_geometry_msgs.h>
// #include <tf2/LinearMath/Quaternion.h>


geometry_msgs::Pose::_orientation_type q;
// geometry_msgs::QuaternionStamped current_attitude;
double roll;
double pitch;
double yaw;

double altitude_offset;
double alt_1;
int alt_counter = 0;

namespace grvc { namespace ual {

BackendDji::BackendDji()
    : Backend()
{
    // Parse arguments
    ros::NodeHandle pnh("~");
    pnh.param<int>("uav_id", robot_id_, 1);
    pnh.param<std::string>("pose_frame_id", pose_frame_id_, "");
    float position_th_param, orientation_th_param;
    pnh.param<float>("position_th", position_th_param, 0.33);
    pnh.param<float>("orientation_th", orientation_th_param, 0.65);
    position_th_ = position_th_param*position_th_param;
    orientation_th_ = 0.5*(1 - cos(orientation_th_param));

    float vel_factor_param;
    pnh.param<float>("vel_factor", vel_factor_param, 0.6);
    vel_factor = vel_factor_param;
    
    pnh.param<bool>("laser_altimeter", laser_altimeter, false);
    pnh.param<bool>("self_arming", self_arming, false);

    ROS_INFO("BackendDji constructor with id %d",robot_id_);
    // ROS_INFO("BackendDji: thresholds = %f %f", position_th_, orientation_th_);

    // // Init ros communications
    ros::NodeHandle nh;
    std::string dji_ns = "dji_sdk";
    // std::string set_mode_srv = mavros_ns + "/set_mode";
    // std::string set_pose_topic = mavros_ns + "/setpoint_position/local";
    // std::string set_pose_global_topic = mavros_ns + "/setpoint_raw/global";
    // std::string set_vel_topic = mavros_ns + "/setpoint_velocity/cmd_vel";
    // std::string pose_topic = mavros_ns + "/local_position/pose";
    // std::string vel_topic = mavros_ns + "/local_position/velocity";
    // std::string state_topic = mavros_ns + "/state";
    // std::string extended_state_topic = mavros_ns + "/extended_state";
    std::string activation_srv = dji_ns + "/activation";
    std::string arming_srv = dji_ns + "/drone_arm_control";
    std::string set_local_pos_ref_srv = dji_ns + "/set_local_pos_ref";
    std::string sdk_control_authority_srv = dji_ns + "/sdk_control_authority";
    std::string drone_task_control_srv = dji_ns + "/drone_task_control";
    std::string get_position_topic = dji_ns + "/local_position";
    std::string get_position_global_topic = dji_ns + "/gps_position";
    std::string get_attitude_topic = dji_ns + "/attitude";
    std::string get_status_topic = dji_ns + "/flight_status";
    std::string get_mode_topic = dji_ns + "/display_mode";

    // std::string flight_control_topic = dji_ns + "/flight_control_setpoint_ENUposition_yaw";
    std::string flight_control_topic = dji_ns + "/flight_control_setpoint_generic";

    std::string get_laser_altitude_topic = "laser_altitude";
    
     


    activation_client_ = nh.serviceClient<dji_sdk::Activation>(activation_srv.c_str());
    arming_client_ = nh.serviceClient<dji_sdk::DroneArmControl>(arming_srv.c_str());
    set_local_pos_ref_client_ = nh.serviceClient<dji_sdk::SetLocalPosRef>(set_local_pos_ref_srv.c_str());
    sdk_control_authority_client_ = nh.serviceClient<dji_sdk::SDKControlAuthority>(sdk_control_authority_srv.c_str());
    drone_task_control_client_ = nh.serviceClient<dji_sdk::DroneTaskControl>(drone_task_control_srv.c_str());

    flight_control_pub_ = nh.advertise<sensor_msgs::Joy>(flight_control_topic.c_str(), 1);
    // mavros_ref_pose_global_pub_ = nh.advertise<mavros_msgs::GlobalPositionTarget>(set_pose_global_topic.c_str(), 1);
    

    flight_status_sub_ = nh.subscribe<std_msgs::UInt8>(get_status_topic.c_str(), 1, \
        [this](const std_msgs::UInt8::ConstPtr& _msg) {
            this->flight_status_ = *_msg;
    });

    display_mode_sub_ = nh.subscribe<std_msgs::UInt8>(get_mode_topic.c_str(), 1, \
        [this](const std_msgs::UInt8::ConstPtr _msg) {
            this->display_mode_ = *_msg;
    });

    position_sub_ = nh.subscribe<geometry_msgs::PointStamped>(get_position_topic.c_str(), 1, \
        [this](const geometry_msgs::PointStamped::ConstPtr& _msg) {
            this->current_position_ = *_msg;
    });

    position_global_sub_ = nh.subscribe<sensor_msgs::NavSatFix>(get_position_global_topic.c_str(), 1, \
        [this](const sensor_msgs::NavSatFix::ConstPtr& _msg) {
            this->current_position_global = *_msg;
    });
    
    attitude_sub_ = nh.subscribe<geometry_msgs::QuaternionStamped>(get_attitude_topic.c_str(), 1, \
        [this](const geometry_msgs::QuaternionStamped::ConstPtr& _msg) {
            this->current_attitude_ = *_msg;
    });

    laser_altitude_sub_ = nh.subscribe<std_msgs::Float64>(get_laser_altitude_topic.c_str(), 1, \
        [this](const std_msgs::Float64::ConstPtr& _msg) {
            this->current_laser_altitude_ = *_msg;
    });

    // // TODO: Check this and solve frames issue
    // // Wait until we have pose
    // while (!mavros_has_pose_ && ros::ok()) {
    //     // ROS_INFO("BackendDji: Waiting for pose");
    //     std::this_thread::sleep_for(std::chrono::milliseconds(200));
    // }
    // initHomeFrame();

    control_thread_ = std::thread(&BackendDji::controlThread, this);
        
    // Wait for dji_sdk_node is running
    ros::service::waitForService("dji_sdk/activation");
    dji_sdk::Activation activation;
    activated_ = activation_client_.call(activation);

    ROS_INFO("BackendDji %d running!", robot_id_);
}

void BackendDji::controlThread() {
    ros::param::param<double>("~dji_offboard_rate", control_thread_frequency_, 30.0);
//     double hold_pose_time = 3.0;  // [s]  TODO param?
//     int buffer_size = std::ceil(hold_pose_time * offboard_thread_frequency_);
//     position_error_.set_size(buffer_size);
//     orientation_error_.set_size(buffer_size);
    ros::Rate rate(control_thread_frequency_);
    while (ros::ok()) {
        sensor_msgs::Joy reference_joy;
        float control_flag;

        switch(control_mode_) {
        case eControlMode::IDLE:
            break;
        case eControlMode::LOCAL_VEL:

            control_flag = (DJISDK::HORIZONTAL_VELOCITY |
                DJISDK::VERTICAL_VELOCITY       |
                DJISDK::YAW_RATE            |
                DJISDK::HORIZONTAL_GROUND |
                DJISDK::STABLE_ENABLE);

            reference_joy.axes.push_back(reference_vel_.twist.linear.x);
            reference_joy.axes.push_back(reference_vel_.twist.linear.y);
            reference_joy.axes.push_back(reference_vel_.twist.linear.z);
            reference_joy.axes.push_back(reference_vel_.twist.angular.z);
            reference_joy.axes.push_back(control_flag);
            flight_control_pub_.publish(reference_joy);

            // reference_pose_.pose.position = current_position_.point;
            if ( ros::Time::now().toSec() - last_command_time_.toSec() >=0.5 ) {
                control_mode_ = eControlMode::IDLE;
            }
            // mavros_ref_vel_pub_.publish(ref_vel_);
            // ref_pose_ = cur_pose_;
            break;
        case eControlMode::LOCAL_POSE: 
                        
            BackendDji::Quaternion2EulerAngle(q, roll, pitch, yaw);
             
            control_flag = (DJISDK::HORIZONTAL_POSITION |
                DJISDK::VERTICAL_POSITION       |
                DJISDK::YAW_ANGLE            |
                DJISDK::HORIZONTAL_GROUND |
                DJISDK::STABLE_ENABLE);
            //flag = (0x80 | 0x10 | 0x00 | 0x02 | 0x01);

            offset_x = reference_pose_.pose.position.x - current_position_.point.x;
            offset_y = reference_pose_.pose.position.y - current_position_.point.y;
            reference_joy.axes.push_back(vel_factor * offset_x);
            reference_joy.axes.push_back(vel_factor * offset_y);
            

            if (laser_altimeter == true) {
                // ROS_INFO("laser alt on: %s", laser_altimeter ? "true":"false");
                if ( current_laser_altitude_.data == 0.0 || altimeter_fail() ) {
                    reference_joy.axes.push_back(reference_pose_.pose.position.z);
                }   
                else {
                    altitude_offset = reference_pose_.pose.position.z - current_laser_altitude_.data;
                    reference_joy.axes.push_back(current_position_.point.z + 2*altitude_offset);
                }
            } else {
                reference_joy.axes.push_back(reference_pose_.pose.position.z);
            }


            reference_joy.axes.push_back(yaw);
            reference_joy.axes.push_back(control_flag);

            flight_control_pub_.publish(reference_joy);

            // std::cout << current_laser_altitude_ << std::endl;

            break; 
        case eControlMode::GLOBAL_POSE:

            control_flag = (DJISDK::HORIZONTAL_POSITION |
                DJISDK::VERTICAL_POSITION       |
                DJISDK::YAW_ANGLE            |
                DJISDK::HORIZONTAL_GROUND |
                DJISDK::STABLE_ENABLE);

            reference_joy.axes.push_back(100000*(reference_pose_global_.longitude - current_position_global.longitude));
            reference_joy.axes.push_back(100000*(reference_pose_global_.latitude - current_position_global.latitude));
            reference_joy.axes.push_back(reference_pose_global_.altitude);
            reference_joy.axes.push_back(yaw);
            reference_joy.axes.push_back(control_flag);

            flight_control_pub_.publish(reference_joy);

            break;
        }
        // // Error history update
        // double dx = ref_pose_.pose.position.x - cur_pose_.pose.position.x;
        // double dy = ref_pose_.pose.position.y - cur_pose_.pose.position.y;
        // double dz = ref_pose_.pose.position.z - cur_pose_.pose.position.z;
        // double positionD = dx*dx + dy*dy + dz*dz; // Equals distance^2

        // double quatInnerProduct = ref_pose_.pose.orientation.x*cur_pose_.pose.orientation.x + \
        // ref_pose_.pose.orientation.y*cur_pose_.pose.orientation.y + \
        // ref_pose_.pose.orientation.z*cur_pose_.pose.orientation.z + \
        // ref_pose_.pose.orientation.w*cur_pose_.pose.orientation.w;
        // double orientationD = 1.0 - quatInnerProduct*quatInnerProduct;  // Equals (1-cos(rotation))/2

        // position_error_.update(positionD);
        // orientation_error_.update(orientationD);

        // State update
        this->state_ = guessState();

        rate.sleep();
    }
}

Backend::State BackendDji::guessState() {
    // Sequentially checks allow state deduction
    if (!this->isReady()) { return UNINITIALIZED; }
    if (this->flight_status_.data == DJISDK::FlightStatus::STATUS_STOPPED) { 
        if (self_arming) {
            return LANDED_ARMED;
        } else {
            return LANDED_DISARMED; 
        }
    }
    if (this->flight_status_.data == DJISDK::FlightStatus::STATUS_ON_GROUND) { return LANDED_ARMED; }
    if (this->calling_takeoff && this->flight_status_.data == DJISDK::FlightStatus::STATUS_IN_AIR ) 
        { return TAKING_OFF; }
    if (this->calling_land && this->flight_status_.data == DJISDK::FlightStatus::STATUS_IN_AIR ) 
        { return LANDING; }
    if (!this->calling_takeoff && !this->calling_land 
        && this->flight_status_.data == DJISDK::FlightStatus::STATUS_IN_AIR 
        // && this->display_mode_.data == 6) 
        && this->display_mode_.data == DJISDK::DisplayMode::MODE_NAVI_SDK_CTRL) 
        // && this->display_mode_.data == DJISDK::DisplayMode::MODE_P_GPS) 
        { return FLYING_AUTO; }

    return FLYING_MANUAL;
}

bool BackendDji::altimeter_fail() {
    double alt_2 = current_laser_altitude_.data;
    if(alt_1 == alt_2 && alt_1 != 0.0){
        alt_counter ++;
    }
    else {
        alt_counter = 0;
    }
    // std::cout << "alt_counter  "<< alt_counter << std::endl;
    alt_1 = alt_2;
    if (alt_counter > 100) return true;
    else return false;
}

void BackendDji::Quaternion2EulerAngle(const geometry_msgs::Pose::_orientation_type& q, double& roll, double& pitch, double& yaw)
{
	// roll (x-axis rotation)
	double sinr = +2.0 * (q.w * q.x + q.y * q.z);
	double cosr = +1.0 - 2.0 * (q.x * q.x + q.y * q.y);
	roll = atan2(sinr, cosr);

	// pitch (y-axis rotation)
	double sinp = +2.0 * (q.w * q.y - q.z * q.x);
	if (fabs(sinp) >= 1)
		pitch = copysign(M_PI / 2, sinp); // use 90 degrees if out of range
	else
		pitch = asin(sinp);

	// yaw (z-axis rotation)
	double siny = +2.0 * (q.w * q.z + q.x * q.y);
	double cosy = +1.0 - 2.0 * (q.y * q.y + q.z * q.z);  
	yaw = atan2(siny, cosy);
}
void BackendDji::setArmed(bool _value) {
    int arm;
    if(_value) {arm=1;}
    else if(!_value) {arm=0;}
    dji_sdk::DroneArmControl arming_service;
    arming_service.request.arm = _value;
    arming_client_.call(arming_service);

//     mavros_msgs::CommandBool arming_service;
//     arming_service.request.value = _value;
//     // Arm: unabortable?
//     while (ros::ok()) {
//         if (!arming_client_.call(arming_service)) {
//             ROS_ERROR("Error in arming service calling!");
//         }
//         std::this_thread::sleep_for(std::chrono::milliseconds(300));
//         ROS_INFO("Arming service response.success = %s", arming_service.response.success ? "true" : "false");
//         ROS_INFO("Trying to set armed to %s... mavros_state_.armed = %s", _value ? "true" : "false", mavros_state_.armed ? "true" : "false");
//         bool armed = mavros_state_.armed;  // WATCHOUT: bug-prone ros-bool/bool comparison 
//         if (armed == _value) { break; }  // Out-of-while condition
//     }
}

// void BackendDji::setFlightMode(const std::string& _flight_mode) {
//     mavros_msgs::SetMode flight_mode_service;
//     flight_mode_service.request.base_mode = 0;
//     flight_mode_service.request.custom_mode = _flight_mode;
//     // Set mode: unabortable?
//     while (mavros_state_.mode != _flight_mode && ros::ok()) {
//         if (!flight_mode_client_.call(flight_mode_service)) {
//             ROS_ERROR("Error in set flight mode [%s] service calling!", _flight_mode.c_str());
//         }
//         std::this_thread::sleep_for(std::chrono::milliseconds(300));
// #ifdef MAVROS_VERSION_BELOW_0_20_0
//         ROS_INFO("Set flight mode [%s] response.success = %s", _flight_mode.c_str(), \
//             flight_mode_service.response.success ? "true" : "false");
// #else
//         ROS_INFO("Set flight mode [%s] response.success = %s", _flight_mode.c_str(), \
//             flight_mode_service.response.mode_sent ? "true" : "false");
// #endif
//         ROS_INFO("Trying to set [%s] mode; mavros_state_.mode = [%s]", _flight_mode.c_str(), mavros_state_.mode.c_str());
//     }
// }

void BackendDji::recoverFromManual() {
    if (display_mode_.data != DJISDK::DisplayMode::MODE_P_GPS) {
        ROS_ERROR("Unable to recover from manual. Not in P_GPS MODE");
        ROS_INFO("Please switch rc to P_GPS MODE");
        return;
    }
    dji_sdk::SDKControlAuthority sdk_control_authority;
    sdk_control_authority.request.control_enable = dji_sdk::SDKControlAuthority::Request::REQUEST_CONTROL;
    sdk_control_authority_client_.call(sdk_control_authority);

    reference_vel_.twist.linear.x = 0;
    reference_vel_.twist.linear.y = 0;
    reference_vel_.twist.linear.z = 0;
    reference_vel_.twist.angular.z = 0;

    control_mode_ = eControlMode::LOCAL_VEL;
    ros::Duration(0.5).sleep();

    control_mode_ = eControlMode::IDLE;  
    
    if(sdk_control_authority.response.result){
        ROS_INFO("Recovered from manual mode!");
    } else {
        ROS_WARN("Unable to recover from manual mode (not in manual!)");
    }
    this->state_ = guessState();
}

void BackendDji::setHome(bool set_z) {

    dji_sdk::SetLocalPosRef set_local_pos_ref;
    set_local_pos_ref_client_.call(set_local_pos_ref);
    home_set_ = true;
    // local_start_pos_ = -Eigen::Vector3d(cur_pose_.pose.position.x, \
    //     cur_pose_.pose.position.y, cur_pose_.pose.position.z);
}

void BackendDji::takeOff(double _height) {
    if (_height < 0.0) {
        ROS_ERROR("Takeoff height must be positive!");
        return;
    }

    if(!home_set_){
        dji_sdk::SetLocalPosRef set_local_pos_ref;
        set_local_pos_ref_client_.call(set_local_pos_ref);
        home_set_ = true;
    }
 
    dji_sdk::SDKControlAuthority sdk_control_authority;
    sdk_control_authority.request.control_enable = dji_sdk::SDKControlAuthority::Request::REQUEST_CONTROL;
    sdk_control_authority_client_.call(sdk_control_authority);

    dji_sdk::DroneTaskControl drone_task_control;
    drone_task_control.request.task = dji_sdk::DroneTaskControl::Request::TASK_TAKEOFF;
    drone_task_control_client_.call(drone_task_control);
    ROS_INFO("Taking Off...");


    calling_takeoff = true;

    reference_pose_.pose.position.x = current_position_.point.x;
    reference_pose_.pose.position.y = current_position_.point.y;
    reference_pose_.pose.position.z = _height;
    q.x = current_attitude_.quaternion.x;
    q.y = current_attitude_.quaternion.y;
    q.z = current_attitude_.quaternion.z;
    q.w = current_attitude_.quaternion.w;

    control_mode_ = eControlMode::LOCAL_POSE;    // Control in position

    while ( !(fabs(_height - current_position_.point.z) < position_th_) ) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // if (flight_status_.data == DJISDK::FlightStatus::STATUS_IN_AIR) {
    // // if(flight_status_ == DJISDK::FlightStatus::STATUS_IN_AIR) {
       
    //     ROS_INFO("Flying!");
    // }
    
    ROS_INFO("Flying!");
    calling_takeoff = false;

    // Update state right now!
    this->state_ = guessState();

    control_mode_ = eControlMode::IDLE;    //Disable control in position
    
    // float f_height = _height;
    // sensor_msgs::Joy reference_joy;
    // reference_joy.axes.push_back(0.0);
    // reference_joy.axes.push_back(0.0);
    // reference_joy.axes.push_back(f_height);
    // reference_joy.axes.push_back(0.0);  // TODO: calculate desired yaw
    // flight_control_pub_.publish(reference_joy);
    // ROS_INFO("height: %f",f_height);

    //control_mode_ = eControlMode::IDLE;  


    // control_mode_ = eControlMode::LOCAL_POSE;  // Take off control is performed in position (not velocity)

    // setArmed(true);
    // ref_pose_ = cur_pose_;
    // ref_pose_.pose.position.z += _height;
    // setFlightMode("OFFBOARD");

    // // Wait until take off: unabortable!
    // while (!referencePoseReached() && ros::ok()) {
    //     std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // }
    
}

void BackendDji::land() {
    calling_land = true;

    dji_sdk::DroneTaskControl drone_task_control;
    drone_task_control.request.task = dji_sdk::DroneTaskControl::Request::TASK_LAND;
    drone_task_control_client_.call(drone_task_control);
   
    if(!drone_task_control.response.result) {
        ROS_ERROR("Land fail");
    }
    else if(drone_task_control.response.result) {
    ROS_INFO("Landing...");
    }

    while (flight_status_.data != DJISDK::FlightStatus::STATUS_STOPPED) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ROS_INFO("Landed!");
    


    control_mode_ = eControlMode::IDLE;  

    calling_land = false;

    // Update state right now!
    this->state_ = guessState();

    // control_mode_ = eControlMode::LOCAL_POSE;  // Back to control in position (just in case)
    // // Set land mode
    // setFlightMode("AUTO.LAND");
    // ROS_INFO("Landing...");
    // ref_pose_ = cur_pose_;
    // ref_pose_.pose.position.z = 0;
    // // Landing is unabortable!
    // while (ros::ok()) {
    //     std::this_thread::sleep_for(std::chrono::milliseconds(100));
    //     if (mavros_extended_state_.landed_state == 
    //         mavros_msgs::ExtendedState::LANDED_STATE_ON_GROUND) { break; }  // Out-of-while condition
    // }
    // setArmed(false);  // Now disarm!
    // ROS_INFO("Landed!");
}

// ros::ServiceServer go_home_service =
//                 nh.advertiseService<std_srvs::Empty::Request, std_srvs::Empty::Response>(
//                 go_home_srv,
//                 [this](std_srvs::Empty::Request &req, std_srvs::Empty::Response &res) {
//                 return this->goHome();

void BackendDji::goHome() {
    dji_sdk::DroneTaskControl drone_task_control;
    drone_task_control.request.task = dji_sdk::DroneTaskControl::Request::TASK_GOHOME;
    drone_task_control_client_.call(drone_task_control);

    control_mode_ = eControlMode::IDLE; 
}
void BackendDji::setVelocity(const Velocity& _vel) {
    control_mode_ = eControlMode::LOCAL_VEL;  // Velocity control!
    reference_vel_ = _vel;

    last_command_time_ = ros::Time::now();
}

bool BackendDji::isReady() const {    
        return activated_;
   }

void BackendDji::setPose(const geometry_msgs::PoseStamped& _world) {
}


void BackendDji::goToWaypoint(const Waypoint& _world) {

    control_mode_ = eControlMode::LOCAL_POSE;    // Control in position
    reference_pose_ = _world;
    q.x = reference_pose_.pose.orientation.x;
    q.y = reference_pose_.pose.orientation.y;
    q.z = reference_pose_.pose.orientation.z;
    q.w = reference_pose_.pose.orientation.w;



//     geometry_msgs::PoseStamped homogen_world_pos;
//     tf2_ros::Buffer tfBuffer;
//     tf2_ros::TransformListener tfListener(tfBuffer);
//     std::string waypoint_frame_id = tf2::getFrameId(_world);

//     if ( waypoint_frame_id == "" || waypoint_frame_id == uav_home_frame_id_ ) {
//         // No transform is needed
//         homogen_world_pos = _world;
//     }
//     else {
//         // We need to transform
//         geometry_msgs::TransformStamped transformToHomeFrame;

//         if ( cached_transforms_.find(waypoint_frame_id) == cached_transforms_.end() ) {
//             // waypoint_frame_id not found in cached_transforms_
//             transformToHomeFrame = tfBuffer.lookupTransform(uav_home_frame_id_, waypoint_frame_id, ros::Time(0), ros::Duration(1.0));
//             cached_transforms_[waypoint_frame_id] = transformToHomeFrame; // Save transform in cache
//         } else {
//             // found in cache
//             transformToHomeFrame = cached_transforms_[waypoint_frame_id];
//         }
        
//         tf2::doTransform(_world, homogen_world_pos, transformToHomeFrame);
        
//     }

// //    std::cout << "Going to waypoint: " << homogen_world_pos.pose.position << std::endl;

//     // Do we still need local_start_pos_?
//     homogen_world_pos.pose.position.x -= local_start_pos_[0];
//     homogen_world_pos.pose.position.y -= local_start_pos_[1];
//     homogen_world_pos.pose.position.z -= local_start_pos_[2];

//     ref_pose_.pose = homogen_world_pos.pose;
//     position_error_.reset();
//     orientation_error_.reset();

//     // Wait until we arrive: abortable
//     while(!referencePoseReached() && !abort_ && ros::ok()) {
//         std::this_thread::sleep_for(std::chrono::milliseconds(100));
//     }
//     // Freeze in case it's been aborted
//     if (abort_ && freeze_) {
//         ref_pose_ = cur_pose_;
//     }
}

void	BackendDji::goToWaypointGeo(const WaypointGeo& _wp){
    control_mode_ = eControlMode::GLOBAL_POSE; // Control in position
    
    reference_pose_global_.latitude = _wp.latitude;
    reference_pose_global_.longitude = _wp.longitude;
    reference_pose_global_.altitude = _wp.altitude;

    // // Wait until we arrive: abortable
    // while(!referencePoseReached() && !abort_ && ros::ok()) {
    //     std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // }
    // // Freeze in case it's been aborted
    // if (abort_ && freeze_) {
    //     ref_pose_ = cur_pose_;
    // }
}

/*void BackendDji::trackPath(const WaypointList &_path) {
    // TODO: basic imlementation, ideally different from a stack of gotos
}*/

Pose BackendDji::pose() {
        Pose out;

        out.pose.position.x = current_position_.point.x;
        out.pose.position.y = current_position_.point.y;
        if (laser_altimeter == true) {
            out.pose.position.z = current_laser_altitude_.data;
        } else {
            out.pose.position.z = current_position_.point.z;
        }
        out.pose.orientation.x = current_attitude_.quaternion.x;
        out.pose.orientation.y = current_attitude_.quaternion.y;
        out.pose.orientation.z = current_attitude_.quaternion.z;
        out.pose.orientation.w = current_attitude_.quaternion.w;

        // if (pose_frame_id_ == "") {
        //     // Default: local pose
        //     out.header.frame_id = uav_home_frame_id_;
        // }
        // else {
        //     // Publish pose in different frame
        //     Pose aux = out;
        //     geometry_msgs::TransformStamped transformToPoseFrame;
        //     std::string pose_frame_id_map = "inv_" + pose_frame_id_;

        //     if ( cached_transforms_.find(pose_frame_id_map) == cached_transforms_.end() ) {
        //         // inv_pose_frame_id_ not found in cached_transforms_
        //         tf2_ros::Buffer tfBuffer;
        //         tf2_ros::TransformListener tfListener(tfBuffer);
        //         transformToPoseFrame = tfBuffer.lookupTransform(pose_frame_id_,uav_home_frame_id_, ros::Time(0), ros::Duration(1.0));
        //         cached_transforms_[pose_frame_id_map] = transformToPoseFrame; // Save transform in cache
        //     } else {
        //         // found in cache
        //         transformToPoseFrame = cached_transforms_[pose_frame_id_map];
        //     }

        //     tf2::doTransform(aux, out, transformToPoseFrame);
        //     out.header.frame_id = pose_frame_id_;
        // }

        return out;
}

Velocity BackendDji::velocity() const {
    // return cur_vel_;
}

Odometry BackendDji::odometry() const {}

Transform BackendDji::transform() const {
    // Transform out;
    // out.header.stamp = ros::Time::now();
    // out.header.frame_id = uav_home_frame_id_;
    // out.child_frame_id = "uav_" + std::to_string(robot_id_);
    // out.transform.translation.x = cur_pose_.pose.position.x + local_start_pos_[0];
    // out.transform.translation.y = cur_pose_.pose.position.y + local_start_pos_[1];
    // out.transform.translation.z = cur_pose_.pose.position.z + local_start_pos_[2];
    // out.transform.rotation = cur_pose_.pose.orientation;
    // return out;
}

// bool BackendDji::referencePoseReached() {

//     double position_min, position_mean, position_max;
//     double orientation_min, orientation_mean, orientation_max;
//     if (!position_error_.metrics(position_min, position_mean, position_max)) { return false; }
//     if (!orientation_error_.metrics(orientation_min, orientation_mean, orientation_max)) { return false; }
    
//     double position_diff = position_max - position_min;
//     double orientation_diff = orientation_max - orientation_min;
//     bool position_holds = (position_diff < position_th_) && (fabs(position_mean) < 0.5*position_th_);
//     bool orientation_holds = (orientation_diff < orientation_th_) && (fabs(orientation_mean) < 0.5*orientation_th_);

//     // if (position_holds && orientation_holds) {  // DEBUG
//     //     ROS_INFO("position: %f < %f) && (%f < %f)", position_diff, position_th_, fabs(position_mean), 0.5*position_th_);
//     //     ROS_INFO("orientation: %f < %f) && (%f < %f)", orientation_diff, orientation_th_, fabs(orientation_mean), 0.5*orientation_th_);
//     //     ROS_INFO("Arrived!");
//     // }

//     return position_holds && orientation_holds;
// }

// void BackendDji::initHomeFrame() {

//     uav_home_frame_id_ = "uav_" + std::to_string(robot_id_) + "_home";
//     local_start_pos_ << 0.0, 0.0, 0.0;

//     // Get frame from rosparam
//     std::string parent_frame;
//     std::vector<double> home_pose(3, 0.0);

//     ros::param::get("~home_pose",home_pose);
//     ros::param::param<std::string>("~home_pose_parent_frame", parent_frame, "map");

//     geometry_msgs::TransformStamped static_transformStamped;

//     static_transformStamped.header.stamp = ros::Time::now();
//     static_transformStamped.header.frame_id = parent_frame;
//     static_transformStamped.child_frame_id = uav_home_frame_id_;
//     static_transformStamped.transform.translation.x = home_pose[0];
//     static_transformStamped.transform.translation.y = home_pose[1];
//     static_transformStamped.transform.translation.z = home_pose[2];

//     if(parent_frame == "map" || parent_frame == "") {
//         static_transformStamped.transform.rotation.x = 0;
//         static_transformStamped.transform.rotation.y = 0;
//         static_transformStamped.transform.rotation.z = 0;
//         static_transformStamped.transform.rotation.w = 1;
//     }
//     else {
//         tf2_ros::Buffer tfBuffer;
//         tf2_ros::TransformListener tfListener(tfBuffer);
//         geometry_msgs::TransformStamped transform_to_map;
//         transform_to_map = tfBuffer.lookupTransform(parent_frame, "map", ros::Time(0), ros::Duration(2.0));
//         static_transformStamped.transform.rotation = transform_to_map.transform.rotation;
//     }

//     static_tf_broadcaster_ = new tf2_ros::StaticTransformBroadcaster();
//     static_tf_broadcaster_->sendTransform(static_transformStamped);
// }

}}	// namespace grvc::ual