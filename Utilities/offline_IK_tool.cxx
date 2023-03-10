#include <OpenSimLiveConfig.h>
#include <OpenSim.h>
#include <IMUPlacerLive.h>
#include <IMUInverseKinematicsToolLive.h>
#include <ThreadPoolContainer.h>
#include <XMLFunctions.h>
#include <future>
#include <functional>
#include <fstream>

const std::string OPENSIMLIVE_ROOT = OPENSIMLIVE_ROOT_PATH;


// Function that is sent to be handled in a separate thread by ThreadPoolContainer
void concurrentIKThread(OpenSimLive::IMUInverseKinematicsToolLive& IKTool, OpenSim::TimeSeriesTable_<SimTK::Quaternion> quatTable, size_t i, bool visualize) {
	//IKTool.setTime(quatTable.getIndependentColumn()[i]);
	IKTool.updateOrdered(visualize, quatTable, i+1, quatTable.getIndependentColumn()[0], true);
}



void performIK(OpenSim::TimeSeriesTable_<SimTK::Quaternion>& quatTable, std::string& calibratedModelFile, unsigned int nThreads, double calibTime, bool visualize)
{
	// do IK
	std::cout << "Performing inverse kinematics..." << std::endl;
	OpenSimLive::IMUInverseKinematicsToolLive IKTool;
	IKTool.setReportErrors(true);
	IKTool.setModelFile(calibratedModelFile); // the model to perform IK on
	IKTool.setQuaternion(quatTable); // the orientations of IMUs
	SimTK::Vec3 sensorToOpenSimRotations = get_sensor_to_opensim_rotations();
	IKTool.setSensorToOpenSimRotations(sensorToOpenSimRotations);
	IKTool.setOpenSimLiveRootDirectory(OPENSIMLIVE_ROOT); // this is needed for saving the IK report to file
	IKTool.setPointTrackerEnabled(false);
	IKTool.setTime(calibTime);
	IKTool.setOutputFileName("IK-offline");
	IKTool.setOutputDataName("IK-offline");
	IKTool.run(visualize);
	try{
		// create a thread pool container with user-given maximum number of concurrent threads
		OpenSimLive::ThreadPoolContainer tpc(nThreads);
		// iterate through all rows (all time / data points) in the quaternion table
		std::cout << "Preparing to iterate through " << quatTable.getNumRows() << " rows." << std::endl;
		for (size_t i = 0; i < quatTable.getNumRows(); ++i) {
			double currentTime = quatTable.getIndependentColumn()[i];
			auto currentMatrix = quatTable.updNearestRow(currentTime);
			OpenSim::TimeSeriesTable_<SimTK::Quaternion> currentQuatTable(std::vector<double>({ currentTime }), currentMatrix.getAsMatrix(), quatTable.getColumnLabels());

			tpc.offerFuture(concurrentIKThread, std::ref(IKTool), currentQuatTable, i, visualize);
			std::cout << "Progress: " << i << "/" << quatTable.getNumRows() << "\r";
			std::cout.flush();
		}
		tpc.waitForFinish();
	}
	catch (std::exception& e) {
		std::cerr << "Error in offline IK tool: " << e.what() << std::endl;
	}
	catch (...) {
		std::cerr << "Unknown error in offline IK tool!" << std::endl;
	}
	std::cout << " done." << std::endl;
	
	IKTool.reportToFile();

}

int main(int argc, char *argv[])
{

	std::string quatFileName;
	std::string inputThreadsStr;
	std::string calibTimeStr;
	std::string startTimeStr;
	std::string endTimeStr;

	if (argc == 6) {
		quatFileName = argv[1];
		inputThreadsStr = argv[2];
		calibTimeStr = argv[3];
		startTimeStr = argv[4];
		endTimeStr = argv[5];
	}
	else if (argc == 1) {

		std::cout << "Please input the name of the text file to be analyzed in /OpenSimLive-results/ folder: ";
		std::cin >> quatFileName;

		std::cout << "Please input the number of threads to be used: ";
		std::cin >> inputThreadsStr;


		std::cout << "Please input the time of IMU calibration: ";
		std::cin >> calibTimeStr;


		std::cout << "Please input IK start time: ";
		std::cin >> startTimeStr;


		std::cout << "Please input IK end time: ";
		std::cin >> endTimeStr;
		
	}
	else {
		std::cout << "Give command line in the following format: offline_IK_tool.exe quaternionTimeSeriesFileName.txt number_of_IK_threads calib_time IK_start_time IK_end_time" << std::endl;
		std::cout << "Alternatively, do not give any command line arguments and the program will let you input them manually." << std::endl;
	}

	int inputThreads = stoi(inputThreadsStr);
	double calibTime = stod(calibTimeStr);
	double startTime = stod(startTimeStr);
	double endTime = stod(endTimeStr);

	bool visualize = false;

	// function that constructs a time series table of quaternions from quaternion time series text file
	OpenSim::TimeSeriesTable_<SimTK::Quaternion> quatTable = quaternionTableFromTextFile(quatFileName);
	// function that calibrates the model and updates calibTime to match the closest similar time in the time series table
	std::string calibratedModelFile = calibrateModel(quatTable, calibTime);
	// function that clips the time series table for IK
	OpenSim::TimeSeriesTable_<SimTK::Quaternion> clippedTable = clipTable(quatTable, startTime, endTime);
	// function that performs IK
	performIK(clippedTable, calibratedModelFile, inputThreads, calibTime, visualize);

	std::cout << "Program finished." << std::endl;
	return 1;
}
