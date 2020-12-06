#include <iostream>

#include <ros/ros.h>
#include <rovi_utils/rovi_utils.h>
#include <rovi_gazebo/rovi_gazebo.h>
#include <rovi_planner/rovi_planner.h>
#include <ur5_controllers/interface.h>
#include <ur5_dynamics/ur5_dynamics.h>

#include "planning_common.hpp"

int
main(int argc, char** argv)
{
	// init nodes
	ros::init(argc, argv, "planning_kdl");
	ros::NodeHandle nh;

	// setup simulation and wait for it to settle
	rovi_gazebo::set_simulation(true);
	rovi_gazebo::set_projector(false);
	ur5_controllers::wsg::release();
	ros::Duration(1).sleep();

	// config
	const auto [MAX_VEL, MAX_ACC, RAD, EQUIV_RAD] = std::tuple{ 0.1, 0.1, 0.05, 0.001 };
	const size_t NUM_ITER = 50;

	// compute via points
	std::vector<geometry_msgs::Pose> waypoints =
	{
		get_current_ee_pose(),                                // starting pose
		get_ee_given_pos(VIA_POINTS["orient"]),               // orient EE and center
		get_ee_given_pos(VIA_POINTS["move-down"]),            // move down
		get_ee_given_pos(VIA_POINTS["pre-fork"]),             // pre-fork
		get_ee_given_pos(VIA_POINTS["fork"]),                 // fork
		get_ee_given_pos(PICK_LOCATIONS[0], PRE_PICK_OFFSET), // pre-obj
		get_ee_given_pos(PICK_LOCATIONS[0], PICK_OFFSET)      // obj
	};
	
	// set experiment name and make dir
	const std::string dir = get_experiment_dir("rovi_system");
	
	// do experiments for linear and parabolic interpolation
	for (const auto method : { "lin", "par" })
	{
		std::vector<PlanningData<KDL::Trajectory_Composite*>> results;
		for (size_t i = 0; i < NUM_ITER and ros::ok(); ++i)
		{
			PlanningData<KDL::Trajectory_Composite*> plan;
			
			auto t_begin = std::chrono::high_resolution_clock::now();
			plan.iteration = i;
			plan.traj = (method == "lin") ? traj_linear(waypoints, MAX_VEL, MAX_ACC, EQUIV_RAD) : traj_parabolic(waypoints, MAX_VEL, MAX_ACC, RAD, EQUIV_RAD);
			plan.planning_time = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t_begin).count();
			plan.traj_duration = plan.traj->Duration();
			
			std::cout << "\n";
			std::cout << "iteration:      " << plan.iteration << std::endl;
			std::cout << "planning time:  " << plan.planning_time << " [ms]" << std::endl;
			std::cout << "traj duration   " << plan.traj_duration << " [s]" << std::endl;
			
			results.push_back(plan);
		}
		
		// export all planning data
		export_planning_data(results, dir + "/plan_" + method + ".csv");
		export_traj(results[0].traj,  dir + "/traj_" + method + ".csv");
	}
	
	// execute end-effector trajectory using Cartesian controller
	std::cout << "\nPress [ENTER] to execute trajectory..." << std::endl;
	std::cin.ignore();
	
	// spawn bottle after one sec
	std::thread([&](){
		auto pos = PICK_LOCATIONS[0].position;
		ros::Duration(1).sleep();
		rovi_gazebo::spawn_model("bottle", "bottle1", { pos.x, pos.y, pos.z });
	}).detach();
	
	// execute trajectory
	auto traj = traj_parabolic(waypoints, MAX_VEL, MAX_ACC, RAD, EQUIV_RAD);
	ur5_controllers::ur5::execute_traj(traj, ur5_controllers::ur5::EXEC_FREQ);
	ROS_INFO_STREAM("Done!");

	// end program
	std::cin.ignore();
	return 0;
}