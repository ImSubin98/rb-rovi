#include <pluginlib/class_list_macros.hpp>
#include <ur5_controllers/joint_position_controller.h>

namespace ur5_controllers
{

	bool
	JointPositionController::init(hardware_interface::EffortJointInterface* hw, ros::NodeHandle& nh)
	{
		// get list of joints (from parameter server)
		if (!nh.getParam("joint_names", vec_joint_names))
		{
			ROS_ERROR_NAMED(CONTROLLER_NAME, "No joints were specifed.");
			return false;
		}

		// get number of joints; exit if zero
		if (num_joints = vec_joint_names.size(); num_joints == 0)
		{
			ROS_ERROR_NAMED(CONTROLLER_NAME, "Vector of joint names is empty.");
			return false;
		}

		// fill vector of joint handles
		for (const auto& joint_name : vec_joint_names)
		{
			try
			{
				vec_joints.push_back(hw->getHandle(joint_name));
			}
			catch (const hardware_interface::HardwareInterfaceException& e)
			{
				ROS_ERROR_NAMED(CONTROLLER_NAME, "Error getting joint handles: %s", e.what());
				return false;
			}
		}

		// subscribe to joint position command
		sub_command = nh.subscribe<ur5_controllers::PoseTwist>("command", 1, &JointPositionController::callback_command, this);

		// init complete
		ROS_INFO_STREAM_NAMED(CONTROLLER_NAME, "Loaded " << CONTROLLER_NAME << " with kp = " << kp << ", kd = " << kd);
		return true;
	}

	void
	JointPositionController::starting(const ros::Time& time)
	{
		geometry_msgs::Pose x_d;
		geometry_msgs::Twist x_dot_d;

		// initial desired position
		for (size_t i = 0; i < Q_D_INIT.size(); i++)
		{
			q_d(i) = Q_D_INIT[i];
			q_dot_d(i) = 0.0f;
		}
		
		// Init pose and command buffer
		x_d = ur5_dynamics::fwd_kin<geometry_msgs::Pose>(q_d);
		x_dot_d.angular.x = 0.0f; x_dot_d.angular.y = 0.0f; x_dot_d.angular.z = 0.0f;
		x_dot_d.linear.x = 0.0f; x_dot_d.linear.y = 0.0f; x_dot_d.linear.z = 0.0f;

		PoseTwist pose_twist;
		pose_twist.pose = x_d; pose_twist.twist = x_dot_d;

		commands_buffer.writeFromNonRT(pose_twist);
	}

	void
	JointPositionController::update(const ros::Time& /*time*/, const ros::Duration& period)
	{
		// elapsed time
		static ros::Duration elapsed_time = ros::Duration(0.);
		elapsed_time += period;

		// get desired joint efforts
		const auto & command = *commands_buffer.readFromRT();

		// read joint states
		const auto q = get_position();
		const auto q_dot = get_velocity();

		// compute dynamics (via KDL)
		const auto g = ur5_dynamics::gravity(q);
		const auto m = ur5_dynamics::mass(q);
		const auto c = ur5_dynamics::coriolis(q, q_dot);

		// compute kinematics (via KDL)
		const auto jac = ur5_dynamics::jac(q);
		const auto pinv_jac = ur5_dynamics::pinv_jac(jac, 0.05);

		// calculate the difference, dx
		Eigen::Vector6d dx;
		Eigen::Vector6d dx_dot;

		// define pose and twist
		{
			geometry_msgs::Pose x_d = command.pose;
			
			// insert into vvector
			x_dot_d << 	command.twist.linear.x, 
						command.twist.linear.y, 
						command.twist.linear.z,
						command.twist.angular.x, 
						command.twist.angular.y, 
						command.twist.angular.z;

			// ik
			q_d = ur5_dynamics::inv_kin<geometry_msgs::Pose>(x_d, q);
		} 

		Eigen::Vector6d tau_des;
		{
			Eigen::DiagonalMatrix<double, 6, 6> kp_m;
			kp_m.diagonal() << kp, kp, kp, kp, kp, kp;

			Eigen::DiagonalMatrix<double, 6, 6> kd_m;
			kd_m.diagonal() << kd, kd, kd, kd, kd, kd;

			const auto y = m * ( kp_m * ( q_d - q ) + kd_m * ( pinv_jac * x_dot_d - q_dot ) );

			tau_des = y + g + c;
		}

		// saturate rate-of-effort (rotatum) this works life a real-life factor xD
		if (SATURATE_ROTATUM)
		    tau_des = saturate_rotatum(tau_des, period.toSec());
		

		// set desired command on joint handles
		for (size_t i = 0; i < num_joints; ++i)
			vec_joints[i].setCommand(tau_des[i]);
	}

	Eigen::Vector6d
	JointPositionController::get_position()
	{
		static Eigen::Vector6d q;

		for (size_t i = 0; i < vec_joints.size(); ++i)
			q[i] = vec_joints[i].getPosition();

		return q;
	}

	Eigen::Vector6d
	JointPositionController::get_velocity()
	{
		static Eigen::Vector6d qdot;

		for (size_t i = 0; i < vec_joints.size(); ++i)
			qdot[i] = vec_joints[i].getVelocity();

		return qdot;
	}

	Eigen::Vector6d
	JointPositionController::saturate_rotatum(const Eigen::Vector6d& tau_des, const double period)
	{
		// previous desired torque and saturated torque
		static Eigen::Vector6d tau_des_prev = Eigen::Vector6d::Zero();
		static Eigen::Vector6d tau_des_sat = Eigen::Vector6d::Zero();

		// compute saturated torque
		for (size_t i = 0; i < tau_des_sat.size(); ++i)
		{
			const double tau_dot = (tau_des[i] - tau_des_prev[i]) / period;
			tau_des_sat[i] = tau_des_prev[i] + std::max(std::min(tau_dot, TAU_DOT_MAX * period), -(TAU_DOT_MAX * period));
		}

		// save for next iteration and return
		tau_des_prev = tau_des_sat;
		return tau_des_sat;
	}

	void
	JointPositionController::callback_command(const ur5_controllers::PoseTwistConstPtr& msg)
	{
		// write commands to command buffer
		commands_buffer.writeFromNonRT(*msg);
	}

}

// export controller
PLUGINLIB_EXPORT_CLASS(ur5_controllers::JointPositionController, controller_interface::ControllerBase)