#pragma once

#include <string>
#include <array>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <tuple>
#include <cstdio>
#include <ctime>
#include <filesystem>

#include <ros/package.h>
#include <Eigen/Eigen>

#include <moveit/planning_scene/planning_scene.h>
#include <geometry_msgs/Pose.h>
#include <moveit_msgs/CollisionObject.h>
#include <std_msgs/ColorRGBA.h>

namespace rovi_utils
{
	// -- geometry_msgs -----------------------------------------------------------

	geometry_msgs::Pose
	make_pose(const std::array<double, 3>& pos, const Eigen::Quaternion<double>& ori);

	geometry_msgs::Pose
	make_pose(const std::array<double, 6>& pose);

	geometry_msgs::Pose
	make_pose(const std::array<double, 3>& pos, const std::array<double, 3>& rpy);
	
	auto
	read_pose(const geometry_msgs::Pose& pose)
	{	
		struct
		{
			std::array<double, 3> pos;
			std::array<double, 3> rpy;
			Eigen::Quaternion<double> q;
		} pose_data;
		
		pose_data.q = Eigen::Quaternion<double>({ pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z });
		const auto euler = pose_data.q.toRotationMatrix().eulerAngles(0, 1, 2);
		
		pose_data.pos = { pose.position.x, pose.position.y, pose.position.z };
		pose_data.rpy = { euler[0], euler[1], euler[2] };
		
		return pose_data;
	}

	// -- trajectories ------------------------------------------------------------

	template<typename T>
	void
	export_traj(T& traj, const std::string& filename, const double resolution = 0.01 /* [s] */);

	std::vector<geometry_msgs::Pose>
	// waypoints_from_moveit_traj()
	waypoints_from_traj(const robot_trajectory::RobotTrajectory& traj);

	std::vector<sensor_msgs::JointState>
	// joint_states_from_moveit_traj()
	joint_states_from_traj(const robot_trajectory::RobotTrajectory& traj);

	// -- moveit ------------------------------------------------------------------

	moveit_msgs::CollisionObject
	make_mesh_cobj(const std::string& name, const std::string& planning_frame, const std::array<double, 3>& pos, const std::array<double, 4>& ori = { 1, 0, 0, 0 });

	void
	set_obj_color(const std::string& name, const std::string& planning_frame, const std_msgs::ColorRGBA& color) {};

	void
	move_base(moveit::core::RobotState& state, const std::array<double, 3>& offset, const std::string& virtual_joint_name = "world_offset");
	
	void
	move_base(moveit::core::RobotState& state, const geometry_msgs::Pose& offset, const std::string& virtual_joint_name = "world_offset");

	// -- utilities ---------------------------------------------------------------

	std::string
	get_timestamp(const std::string& format = "%Y%m%d_%H%M%S")
	{
		std::time_t rawtime;
		std::tm* timeinfo;
		char buffer [80];
		std::time(&rawtime);
		timeinfo = std::localtime(&rawtime);
		std::strftime(buffer, 80, format.c_str(), timeinfo);
		std::puts(buffer);
		return std::string(buffer);
	}
	
	std::string
	get_data_path(const std::string& package)
	{
		return ros::package::getPath(package) + "/data";
	}
	
	std::string
	get_experiment_dir(const std::string& package)
	{
		auto dir = get_data_path(package) + ros::this_node::getName() + "/" + get_timestamp();
		std::filesystem::create_directories(dir);
		return dir;
	}
	
	template <typename T>
	std::thread*
	create_async_listener(const std::string& topic, T& obj, std::mutex& mutex)
	{
		// !!! a static map of topic names and thread would make this function more safe

		// example usage:

		// static std::mutex mtx_joint_states;
		// static sensor_msgs::JointState joint_states;
		// static auto thread = create_async_listener("/joint_states", joint_states, mtx_joint_states);

		// std::lock_guard<std::mutex> lock(mtx_joint_states);
		// return joint_states;

		std::atomic<bool> got_message = false;

		auto t = new std::thread([&, topic]()
		{

			ROS_WARN_STREAM("Initializing /listener" << topic << " node (once)...");
			auto nh = new ros::NodeHandle("~/listener" + topic);

			const auto sub_topic = nh->subscribe<T>(topic, 1, [&, has_been_called = size_t(false)](const auto& msg) mutable
			{
				std::lock_guard<std::mutex> lock(mutex);
				obj = *msg;

				if (not has_been_called)
					got_message = bool(++has_been_called);
			});

			ros::Rate lp(100); // Hz
			while (ros::ok())
			{
				ros::spinOnce();
				lp.sleep();
			}

		});

		// detach thread (once)
		t->detach();

		// wait for first message
		while (not got_message);

		// return thread handle
		return t;
	}
}