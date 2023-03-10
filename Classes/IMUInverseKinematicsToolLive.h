// This class calculates inverse kinematics aka joint angles of the OpenSim model based on live data measured with IMUs.

#pragma once

#include <OpenSim/Common/Object.h>
#include <OpenSim/Common/ModelDisplayHints.h>
#include <OpenSim/Common/Set.h>
#include <OpenSim/Common/TimeSeriesTable.h>
#include <OpenSim/Simulation/Model/Point.h>
#include <OpenSim/Simulation/OrientationsReference.h>
#include <PointTracker.h>
#include <array>
#include <atomic>

namespace OpenSimLive {

	class IMUInverseKinematicsToolLive : public OpenSimLive::PointTracker {

	public:
		// CONSTRUCTORS AND DECONSTRUCTORS
		IMUInverseKinematicsToolLive();
		IMUInverseKinematicsToolLive(const std::string& modelFile);
		IMUInverseKinematicsToolLive(const std::string& modelFile, const OpenSim::TimeSeriesTable_<SimTK::Quaternion>& quatTable);
		~IMUInverseKinematicsToolLive();

		// PUBLIC METHODS DEFINED IN THE .CXX FILE
		void runInverseKinematicsWithLiveOrientations(OpenSim::Model& model, OpenSim::TimeSeriesTable_<SimTK::Quaternion>& quatTable, const bool visualizeResults = false);
		bool IMUInverseKinematicsToolLive::run(const bool visualizeResults);
		void IMUInverseKinematicsToolLive::update(const bool visualizeResults, const bool offline = false);
		void IMUInverseKinematicsToolLive::update(const bool visualizeResults, OpenSim::TimeSeriesTable_<SimTK::Quaternion>& quat, const bool offline = false);
		void IMUInverseKinematicsToolLive::updateOrdered(const bool visualizeResults, OpenSim::TimeSeriesTable_<SimTK::Quaternion>& quat, unsigned int orderIndex, double time, const bool offline = false);
		void reportToFile();
		void setPointTrackerOutputFormat(const std::string& outputFormat);
		void setPointTrackerEulerConvention(const std::string& eulerConvention);

		// PUBLIC METHODS DEFINED HERE
		bool get_report_errors() { return report_errors; }
		std::vector<double> getQ() { return q_; }
		std::vector<double> getPointTrackerPositionsAndOrientations() { return pointTrackerPositionsAndOrientations_; }
		void setTime(const double time) { time_ = time; }
		double getTime() { return time_; }
		void setQuaternion(const OpenSim::TimeSeriesTable_<SimTK::Quaternion>& newQuat) { quat_ = newQuat; }
		void setModel(const OpenSim::Model& newModel) { model_ = newModel; }
		void setModelFile(const std::string& newModelFile) { model_ = OpenSim::Model(newModelFile); }
		void setOpenSimLiveRootDirectory(const std::string& directoryPath) { OpenSimLiveRootDirectory_ = directoryPath; }
		void setSensorToOpenSimRotations(const SimTK::Vec3& newRotations) { sensor_to_opensim_rotations = newRotations; }
		void setPointTrackerReferenceBodyName(const std::string& referenceBodyName) { pointTrackerReferenceBodyName_ = referenceBodyName; }
		void setPointTrackerBodyName(const std::string& bodyName) { pointTrackerBodyName_ = bodyName; }
		void setPointTrackerTransformToKuka(bool setting) { setTransformRotationsToKuka(setting); }
		void setReportErrors(bool report) { report_errors = report; }
		// set accuracy for IK solver
		void setAccuracy(double accuracy) { accuracy_ = accuracy; }
		// set the name of the IK output file
		void setOutputFileName(std::string newFileName) { outputFileName_ = newFileName; }
		// set the name of the data inside the IK output file
		void setOutputDataName(std::string newDataName) { outputDataName_ = newDataName; }
		//void setVisualizedJointAnglesVector(std::vector<std::string> vector) { visualizedJointAnglesVector_ = vector; }
		void setVisualizeAllFrames(bool setting) { visualizeAllFrames_ = setting; }

	private:
		// PRIVATE VARIABLES
		std::string OpenSimLiveRootDirectory_ = "";
		// name of the IK output file without file format suffix
		std::string outputFileName_ = "IK-live";
		// name of the IK output data
		std::string outputDataName_ = "IK-live";
		double accuracy_ = 1e-5;
		OpenSim::TimeSeriesTable* modelOrientationErrors_;
		OpenSim::TableReporter* ikReporter_ = NULL;
		SimTK::State s_; // the state we use for visualization
		double time_ = 0; // time since calibration
		SimTK::Vec3 sensor_to_opensim_rotations = { -1.5707963267948966, 0, 0 }; // the rotations applied to IMU coordinate systems to match its axes to OpenSim coordinate system
		OpenSim::TimeSeriesTable_<SimTK::Quaternion> quat_; // quaternion data for IMU orientation
		OpenSim::Set<OpenSim::OrientationWeight> orientationWeightSet; // weights for individual IMUs
		bool report_errors = true;
		OpenSim::Model model_; // the OpenSim model that state s_ depicts
		std::vector<double> q_; // joint angles calculated from IK are stored here
		std::vector<double> pointTrackerPositionsAndOrientations_;
		std::string pointTrackerBodyName_ = "";
		std::string pointTrackerReferenceBodyName_ = "pelvis";
		std::string pointTrackerOutputFormat_ = "euler"; // Euler angles by default
		double lastUpdatedTime_ = 0;
		// labels of sensors (e.g. "pelvis_imu")
		SimTK::Array_<std::string> labels_;
		// atomic integer serving as temporary fake time for states that we use to realize report for ikReporter_
		std::atomic<unsigned long> atomicTimeIndex_ = 0;

		std::atomic<unsigned int> debugCounter1_ = 0;
		std::atomic<unsigned int> debugCounter2_ = 0;
		
		// for ordered IK
		std::vector<unsigned int> orderedIndexVector_;
		std::vector<double> orderedTimeVector_;

		// for visualizing IK
		bool visualizeAllFrames_ = false;

		// all coordinates that are defined in the model; defined after calibration
		std::unique_ptr<OpenSim::ComponentList<OpenSim::Coordinate>> modelCoordinates_;
		// vector of coordinates named by the user that we want to visualize for each time point with a slider and print on the console 
		std::vector<std::string> trackedCoordinateNames_;
		// values of the tracked joint angles
		std::vector<SimTK::Real> trackedCoordinateValues_;
		// whether to print tracked coordinates to console
		bool printTrackedCoordinates_ = false;

		// PRIVATE METHODS DEFINED HERE
		SimTK::Vec3 get_sensor_to_opensim_rotations() { return sensor_to_opensim_rotations; }
		OpenSim::Set<OpenSim::OrientationWeight> get_orientation_weights() { return orientationWeightSet; }
		OpenSim::TimeSeriesTable_<SimTK::Quaternion> get_quat() { return quat_; }
		OpenSim::Model get_model() { return model_; }
		void setQ(const std::vector<double>& q) { q_ = q; }
		void setPointTrackerPositionsAndOrientations(const std::vector<double>& positionsAndOrientations) { pointTrackerPositionsAndOrientations_ = positionsAndOrientations; }
		std::string getPointTrackerBodyName() { return pointTrackerBodyName_; }
		std::string getPointTrackerReferenceBodyName() { return pointTrackerReferenceBodyName_; }

		// PRIVATE METHODS DEFINED IN THE .CXX FILE
		void updateInverseKinematics(OpenSim::TimeSeriesTable_<SimTK::Quaternion>& quatTable, const bool visualizeResults = false, const bool offline = false);
		void updateOrderedInverseKinematics(OpenSim::TimeSeriesTable_<SimTK::Quaternion>& quatTable, unsigned int orderIndex, double time, const bool visualizeResults = false, const bool offline = false);
		void updateJointAngleVariable(SimTK::State& s, OpenSim::Model& model);
		void updatePointTracker(SimTK::State s);
		void startDecorationGenerator();

	};  // end of class

}