// This class calculates the position of a mirrored point and the orientation of its parent body for mirror therapy applications.

#pragma once

#include <OpenSim.h>
#include <vector>
#include <deque>
#include <DecorationGeneratorLive.h>

namespace OpenSimLive {

	class PointTracker {

		

	public:
		// CONSTRUCTORS AND DECONSTRUCTORS
		PointTracker();
		PointTracker(SimTK::State state, std::string modelFileName);
		PointTracker(SimTK::State state, std::string modelFileName, std::string bodyName);
		~PointTracker();

		// PUBLIC METHODS
		void addStationToBody(const std::string& bodyName, const SimTK::Vec3& pointLocation, const std::string& modelFile); // adds the station to the desired body in the OpenSim model through XML manipulation
		std::vector<double> runTracker(const SimTK::State* s, OpenSim::Model* model, const std::string& bodyName, const std::string& referenceBodyName); // runs the PointTracker methods that calculate mirrored position and orientation, called from IMUInverseKinematicsToolLive
		void setPointTrackerEnabled(const bool setting) { pointTrackerEnabled_ = setting; }
		// Get the current orientation of the sensor on the reference body (placed where the robot arm is mounted when this method is called) and save it as a quaternion to a variable.
		void setReferenceBaseRotation(SimTK::Quaternion_<SimTK::Real> quatVector) { referenceBaseRotation_ = quatVector; }
		void setReferenceBodyRotation(SimTK::Quaternion_<SimTK::Real> quatVector) { referenceBodyRotation_ = quatVector; }
		void setSavePointTrackerResults(bool setting) { savePointTrackerResults_ = setting; }
		bool getSavePointTrackerResults() { return savePointTrackerResults_; }
		bool getUseReferenceRotation() { return useReferenceRotation_; }
		void setUseReferenceRotation(bool setting) { useReferenceRotation_ = setting; }

	protected:
		// PROTECTED ENUMERATIONS
		enum OutputRotation { EULER = 0, QUATERNION = 1 };

		// PROTECTED METHODS
		bool getPointTrackerEnabled() { return pointTrackerEnabled_; }
		void createDecorationGenerator() { decGen_ = new OpenSimLive::DecorationGeneratorLive(); } // create decoration generator so that we can draw geometry in the visualization window
		void setVisualize(bool setting) { visualize_ = setting; }
		void setPointTrackerCurrentTime(double time) { timeSeriesCurrentTime_ = time; }
		void savePointTrackerOutputToFile(std::string& rootDir, std::string& resultsDir);
		void setPointTrackerOutputRotation(OutputRotation outputFormat) { outputFormat_ = outputFormat; }
		void setTransformRotationsToKuka(bool setting) { transformRotationsToKuka_ = setting; }
		void setPointTrackerBodyOrSpaceType(SimTK::BodyOrSpaceType type) { bodyOrSpaceType_ = type; }
		void setPointTrackerCoordinateAxes(size_t index, SimTK::CoordinateAxis axis) { coordinateAxes_[index] = axis; }

		// PROTECTED VARIABLES
		OpenSimLive::DecorationGeneratorLive* decGen_;
		

	private:
		// PRIVATE METHODS
		void calculateBodyToBaseMatrix();
		SimTK::Vec3 findStationLocationInLocalFrame(OpenSim::Model* model, const std::string& bodyName);
		SimTK::Vec3 calculatePointLocation(const SimTK::Vec3& localLocation, const SimTK::State& s, const OpenSim::Body* body, const OpenSim::Body* referenceBody);
		SimTK::Rotation calculatePointRotation(const SimTK::State* s, OpenSim::Model* model, const OpenSim::Body* body, const OpenSim::Body* referenceBody);
		void reflectWithRespectToAxis(SimTK::Vec3& pointLocation, const int axisIndex);
		
		//PRIVATE VARIABLES
		std::string referenceBodyName_ = "pelvis";
		std::string bodyName_ = "";
		bool pointTrackerEnabled_ = false;
		bool visualize_ = false;
		bool useReferenceRotation_ = false;
		SimTK::Quaternion_<SimTK::Real> referenceBaseRotation_; // quaternion rotation of the IMU that is now on station_reference_body, but was on the base of the robot arm when this variable was saved 
		SimTK::Quaternion_<SimTK::Real> referenceBodyRotation_; // quaternion rotation of that IMU on station_reference_body whenever IK is calculated
		bool savePointTrackerResults_ = false; // whether to save PointTracker output to file 
		double timeSeriesCurrentTime_; // current time, obtained from IMUInverseKinematicsToolLive if savePOintTrackerResults_ is true
		std::vector<double> timeSeriesTimeVector_; // simply a vector containing the time points of IK
		std::vector<std::vector<double>> timeSeriesDepData_; // a vector that will contain the outputs of PointTracker (mirrored point locations and rotations)
		SimTK::Rotation mirroredRotation_; // used to pass mirrored rotation to decoration generator
		SimTK::Rotation bodyToBase_; // base segment (body) to robot base rotation for L-correction
		
		OutputRotation outputFormat_ = EULER;
		bool transformRotationsToKuka_ = false;


		SimTK::BodyOrSpaceType bodyOrSpaceType_ = SimTK::BodyOrSpaceType::BodyRotationSequence;
		std::array<SimTK::CoordinateAxis, 3> coordinateAxes_ = {SimTK::ZAxis, SimTK::YAxis, SimTK::XAxis};

	}; // end of class
}