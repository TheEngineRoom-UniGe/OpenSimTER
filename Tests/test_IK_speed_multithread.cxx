// OpenSimLive.cpp : This file contains the 'main' function. Program execution begins and ends there.

#include <OpenSimLiveConfig.h>
#include <OpenSim.h>
#include <IMUPlacerLive.h>
#include <IMUInverseKinematicsToolLive.h>
#include <ThreadPoolContainer.h>
#include "conio.h" // for non-ANSI _kbhit() and _getch()
#include <XMLFunctions.h>
#include <XMLFunctionsXsens.h>
#include <thread>
#include <future>
#include <functional>
#include <IMUHandler.h>

const std::string OPENSIMLIVE_ROOT = OPENSIMLIVE_ROOT_PATH;

//std::vector<double> enterTimes;
//std::vector<double> finishTimes;

void updateConcurrentIKTool(OpenSimLive::IMUInverseKinematicsToolLive& IKTool, std::chrono::steady_clock::time_point& clockStart, std::chrono::duration<double>& clockDuration, OpenSim::TimeSeriesTable_<SimTK::Quaternion> quatTable) {
	
	// calculate current duration
	clockDuration = (std::chrono::high_resolution_clock::now() - clockStart);
	// update current duration as time in IKTool
	IKTool.setTime(clockDuration.count());

	IKTool.update(false, quatTable);
	
	std::vector<double> trackerResults = IKTool.getPointTrackerPositionsAndOrientations();
	size_t packetSize = trackerResults.size();
	//double* mirrorTherapyPacket = &trackerResults[0];
	double* mirrorTherapyPacket = trackerResults.data();
	//finishTimes.push_back((std::chrono::high_resolution_clock::now() - clockStart).count());
}

void ConnectToDataStream(double inputSeconds, int inputThreads) {
	//enterTimes.reserve(100000);
	//finishTimes.reserve(100000);
	OpenSimLive::IMUHandler genericDataReader;

	std::string manufacturerStr = ConfigReader("MainConfiguration.xml", "IMU_manufacturer");
	OpenSimLive::IMUHandler::IMUType manufacturer = OpenSimLive::IMUHandler::IMUType::simulated; // default to simulated in case the following if-statements fail
	if (manufacturerStr == "delsys")
		manufacturer = OpenSimLive::IMUHandler::IMUType::delsys;
	else if (manufacturerStr == "xsens")
		manufacturer = OpenSimLive::IMUHandler::IMUType::xsens;
	else if (manufacturerStr == "simulated")
		manufacturer = OpenSimLive::IMUHandler::IMUType::simulated;

	genericDataReader.setManufacturer(manufacturer);

	genericDataReader.initialize();

		
	std::string calibratedModelFile; // the file name of the calibrated OpenSim model will be stored here
	bool saveIKResults = ("true" == ConfigReader("MainConfiguration.xml", "save_ik_results")); // Boolean that determines if we want to save IK results (joint angles and their errors) to file when the program finishes. This can be very memory-heavy especially with long measurements.
	bool enableMirrorTherapy = (ConfigReader("MainConfiguration.xml", "station_parent_body") != "none"); // if "none", then set to false

	

	OpenSimLive::IMUInverseKinematicsToolLive IKTool; // object that calculates IK
	IKTool.setReportErrors(saveIKResults);

	// get the sensor to opensim rotations for IMUInverseKinematicsToolLive
	SimTK::Vec3 sensorToOpenSimRotations = get_sensor_to_opensim_rotations();
	
	// CALIBRATION STEP
	genericDataReader.updateQuaternionTable();
	// fill a timeseriestable with quaternion orientations of IMUs
	OpenSim::TimeSeriesTable_<SimTK::Quaternion>  quaternionTimeSeriesTable = genericDataReader.getQuaternionTable();
	// calibrate the model and return its file name
	calibratedModelFile = calibrateModelFromSetupFile(OPENSIMLIVE_ROOT + "/Config/" + ConfigReader("MainConfiguration.xml", "imu_placer_setup_file"), quaternionTimeSeriesTable);
	// give IKTool the necessary inputs and run it
	IKTool.setModelFile(calibratedModelFile); // the model to perform IK on
	IKTool.setQuaternion(quaternionTimeSeriesTable); // the orientations of IMUs
	IKTool.setSensorToOpenSimRotations(sensorToOpenSimRotations);
	IKTool.setTime(0); // set the time of the first state as 0 at calibration
	IKTool.setOpenSimLiveRootDirectory(OPENSIMLIVE_ROOT); // this is needed for saving the IK report to file
	IKTool.run(false); // true for visualization
	std::cout << "Model has been calibrated." << std::endl;
	// set private variables to be accessed in IK calculations
	if (enableMirrorTherapy == true) {
		IKTool.setPointTrackerBodyName(ConfigReader("MainConfiguration.xml", "station_parent_body"));
		IKTool.setPointTrackerReferenceBodyName(ConfigReader("MainConfiguration.xml", "station_reference_body"));
		IKTool.setPointTrackerEnabled(true);
		IKTool.setSavePointTrackerResults(true);
	}
	else {
		IKTool.setPointTrackerEnabled(false);
	}

	unsigned int maxThreads = inputThreads;
	//std::vector<std::future<void>> futureVector;

	//ThreadPool threadPool(maxThreads);
	OpenSimLive::ThreadPoolContainer threadPoolContainer(maxThreads);

	std::cout << "Entering measurement loop." << std::endl;
	int iteration = 0;
	auto clockStart = std::chrono::high_resolution_clock::now(); // get the starting time of IMU measurement loop
	std::chrono::duration<double> clockDuration;

	std::cout << clockDuration.count() << std::endl;
	// loop until enough time has been elapsed
	//auto table = genericDataReader.updateAndGetQuaternionTable();
	do {

		// update quaternions for IKTool
		//genericDataReader.updateQuaternionTable();
		//IKTool.setQuaternion(genericDataReader.getQuaternionTable());
		//enterTimes.push_back((std::chrono::high_resolution_clock::now() - clockStart).count());
		// begin multithreading a function that consists of IK calculations + PointTracker
		threadPoolContainer.offerFuture(updateConcurrentIKTool, std::ref(IKTool), std::ref(clockStart), std::ref(clockDuration), genericDataReader.updateAndGetQuaternionTable());

		// increment iterations number
		++iteration;

	} while (clockDuration.count() < inputSeconds);

	double finalTime = clockDuration.count();

	std::cout << "Performed " << iteration << " iterations in " << finalTime << " seconds." << std::endl;
	std::cout << "Frame rate: " << ((double)iteration / finalTime) << " iterations per second." << std::endl;

	if (saveIKResults) {
		std::cout << "Saving IK results to file..." << std::endl;
		if (iteration < 100000) {
			IKTool.reportToFile();
		}
		else
		{
			std::cout << "More than 100 000 iterations calculated, as a safety precaution program is not saving results to file!" << std::endl;
		}
	}

	/*std::vector<double> IKTimes;
	double mean = 0;
	for (unsigned int j = 0; j < finishTimes.size(); ++j) {
		IKTimes.push_back(finishTimes[j]-enterTimes[j]);
		std::cout << finishTimes[j] << " - " << enterTimes[j] << " = " << IKTimes[j] << std::endl;
		mean += IKTimes[j];
	}
	std::cout << "Mean: " << mean/(double)finishTimes.size() << std::endl;*/

	// close the connection to IMUs
	genericDataReader.closeConnection();

	return;
}


int main(int argc, char *argv[])
{
	std::string inputSecsStr;
	std::cout << "Please input test duration in seconds: ";
	std::cin >> inputSecsStr;
	double inputSecs = stod(inputSecsStr);

	std::string inputThreadsStr;
	std::cout << "Please input the number of threads to be used: ";
	std::cin >> inputThreadsStr;
	int inputThreads = stoi(inputThreadsStr);

	std::cout << "Connecting..." << std::endl;
	// connect to XSens IMUs, perform IK etc
	ConnectToDataStream(inputSecs, inputThreads);

	std::cout << "Program finished." << std::endl;
	return 1;
}
