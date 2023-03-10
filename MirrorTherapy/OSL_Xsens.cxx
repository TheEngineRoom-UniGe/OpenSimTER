// OpenSimLive.cpp : This file contains the 'main' function. Program execution begins and ends there.

#include <OpenSimLiveConfig.h>
#include <OpenSim.h>
#include <IMUPlacerLive.h>
#include <IMUInverseKinematicsToolLive.h>
#include <Server.h>
#include <XsensDataReader.h>
#include "conio.h" // for non-ANSI _kbhit() and _getch()
#include <XMLFunctions.h>
#include <XMLFunctionsXsens.h>
#include <ThreadPoolContainer.h>

const std::string OPENSIMLIVE_ROOT = OPENSIMLIVE_ROOT_PATH;

// Holds various variables that are passed to several functions so that function arguments can be reduced by passing only this struct or a reference to it.
struct VariableManager {
	std::string calibratedModelFile; // the file name of the calibrated OpenSim model will be stored here
	bool mainDataLoop = true; // IMU data is being measured and analyzed while this is true
	bool continuousMode = false; // IK is calculated continuously while this is true
	bool sendMode = false; // data is sent to client with each IK step while this is true
	bool saveIKResults = ("true" == ConfigReader("MainConfiguration.xml", "save_ik_results")); // Boolean that determines if we want to save IK results (joint angles and their errors) to file when the program finishes. This can be very memory-heavy especially with long measurements.
	int continuousModeMsDelay = std::stoi(ConfigReader("MainConfiguration.xml", "continuous_mode_ms_delay")); // delay between consequent IK calculations in consequent mode, in milliseconds
	bool print_roll_pitch_yaw = ("true" == ConfigReader("MainConfiguration.xml", "print_roll_pitch_yaw")); // boolean that tells whether to print roll, pitch and yaw of IMUs while calculating IK
	bool resetClockOnContinuousMode = ("true" == ConfigReader("MainConfiguration.xml", "reset_clock_on_continuous_mode")); // if true, clock will be reset to zero when entering continuous mode; if false, the clock will be set to zero at calibration
	bool enableMirrorTherapy = (ConfigReader("MainConfiguration.xml", "station_parent_body") != "none"); // if "none", then set to false
	unsigned int maxThreads = stoi(ConfigReader("MainConfiguration.xml", "threads")); // get the maximum number of concurrent threads for multithreading
	std::string stationReferenceBody = ConfigReader("MainConfiguration.xml", "station_reference_body"); // get the name of the reference body used in mirror therapy

	std::chrono::steady_clock::time_point clockStart = std::chrono::high_resolution_clock::now(); // get the starting time of IMU measurement loop
	std::chrono::steady_clock::time_point clockNow = std::chrono::high_resolution_clock::now(); // this value will be updated in the loop
	std::chrono::steady_clock::time_point clockPrev = clockStart; // this value will be updated in the loop to present the time point of the previous IK calculation
	std::chrono::duration<double> clockDuration;
	std::chrono::duration<double> prevDuration;
}; // struct dataHolder ends

// This function prints roll, pitch and yaw values of the Xsens sensors in case we want to see them.
void printRollPitchYaw(std::vector<MtwCallback*> mtwCallbacks, const std::vector<XsEuler>& eulerData) {
	for (size_t i = 0; i < mtwCallbacks.size(); ++i)
	{
		std::cout << "[" << i << "]: ID: " << mtwCallbacks[i]->device().deviceId().toString().toStdString()
			<< ", Roll: " << std::setw(7) << std::fixed << std::setprecision(2) << eulerData[i].roll()
			<< ", Pitch: " << std::setw(7) << std::fixed << std::setprecision(2) << eulerData[i].pitch()
			<< ", Yaw: " << std::setw(7) << std::fixed << std::setprecision(2) << eulerData[i].yaw()
			<< std::endl;
	}
}

// IK for multithreading, each thread runs this function separately
void concurrentIK(OpenSimLive::IMUInverseKinematicsToolLive& IKTool, const VariableManager& vm, Server& myLink) {
	IKTool.setTime(vm.clockDuration.count());
	// calculate the IK and update the visualization
	IKTool.update(true);
	if (vm.enableMirrorTherapy)
	{
		// get the data we want to send to Java program
		std::vector<double> trackerResults = IKTool.getPointTrackerPositionsAndOrientations();
		size_t packetSize = trackerResults.size();
		// get a double array from the double std::array
		//double* mirrorTherapyPacket = &trackerResults[0];
		// create mirrorTherapyPacket as a pointer to the underlying array of trackerResults
		double* mirrorTherapyPacket = trackerResults.data();
		// send the data
		if (vm.sendMode)
			myLink.SendDoubles(mirrorTherapyPacket, packetSize);
	}
}

// Runs IK and related shenanigans; this function is not yet multithreaded, but it contains the statement to begin multithreading
void RunIKProcedure(OpenSimLive::XsensDataReader& xsensDataReader, std::vector<XsQuaternion>& quaternionData, OpenSimLive::IMUInverseKinematicsToolLive& IKTool, const std::vector<XsEuler>& eulerData, Server& myLink, OpenSimLive::ThreadPoolContainer& threadPoolContainer, const VariableManager& vm) {
	// fill a time series table with quaternion orientations of the IMUs
	OpenSim::TimeSeriesTable_<SimTK::Quaternion> quatTable(fillQuaternionTable(xsensDataReader.GetMtwCallbacks(), quaternionData));

	// get the orientation of station_reference_body and pass it to PointTracker through IKTool; MOVE THIS TO CONCURRENTIK TO SPEED UP PROGRAM SLIGHTLY
	if (IKTool.getUseReferenceRotation())
	{
		bool foundBodyIMUOrientation = false;
		for (unsigned int i = 0; i < quatTable.getNumColumns(); ++i) // iterate through the quaternions of all IMUs
		{
			if (vm.stationReferenceBody + "_imu" == quatTable.getColumnLabel(i)) // if we find the data of the IMU on station_reference_body
			{
				// get the quaternion orientation of that IMU
				SimTK::Quaternion_<SimTK::Real> quat = quatTable.getMatrixBlock(0, i, 1, 1)[0][0];
				// pass it on to IKTool that inherits PointTracker, and use it at the end of PointTracker rotation calculations
				IKTool.setReferenceBodyRotation(quat);
				foundBodyIMUOrientation = true;
				break;
			}
		}
		if (!foundBodyIMUOrientation)
		{
			std::cout << "Base body IMU orientation not found! Disabling feature." << std::endl;
			IKTool.setUseReferenceRotation(false);
		}
	}

	// give the necessary inputs to IKTool
	IKTool.setQuaternion(quatTable);

	// Send a function to be multithreaded
	threadPoolContainer.offerFuture(concurrentIK, std::ref(IKTool), std::ref(vm), std::ref(myLink));

	if (vm.print_roll_pitch_yaw)
		printRollPitchYaw(xsensDataReader.GetMtwCallbacks(), eulerData);
}








int main(int argc, char *argv[])
{

	// create Xsens connection object and connect the program to IMUs
	OpenSimLive::XsensDataReader xsensDataReader;
	int desiredUpdateRate = stoi(ConfigReader("MainConfiguration.xml", "desired_update_rate"));
	xsensDataReader.SetDesiredUpdateRate(desiredUpdateRate);

	unsigned int dataReaderResult = 0;
	while (dataReaderResult == 0) {
		dataReaderResult = xsensDataReader.InitiateStartupPhase();
	}
	// if InitiateStartupPhase returned 1, we close the program
	if (dataReaderResult == 1) {
		xsensDataReader.CloseConnection();
		return 1;
	}

	std::vector<XsEuler> eulerData(xsensDataReader.GetMtwCallbacks().size()); // Room to store euler data for each mtw
	std::vector<XsQuaternion> quaternionData(xsensDataReader.GetMtwCallbacks().size()); // for data in quaternion form

	// Create a struct to hold a number of variables, and to pass them to functions
	VariableManager vm;

	bool getDataKeyHit = false; // tells if the key that initiates a single IK calculation is hit
	bool referenceBaseRotationKeyHit = false; // tells if the key that initiates the fetching of the current rotation of the base IMU is hit
	bool calibrateModelKeyHit = false; // tells if the key that initiates model calibration is hit
	bool startContinuousModeKeyHit = false; // tells if the key that starts continuous mode is hit
	bool stopContinuousModeKeyHit = false; // tells if the key that pauses continuous mode is hit
	bool startSendModeKeyHit = false; // tells if the key that starts send mode is hit
	bool stopSendModeKeyHit = false; // tells if the key that pauses send mode is hit

	// initialize the object that handles multithreading
	OpenSimLive::ThreadPoolContainer threadPoolContainer(vm.maxThreads);

	// get the current times
	vm.clockStart = std::chrono::high_resolution_clock::now();
	vm.clockNow = vm.clockStart;
	vm.clockPrev = vm.clockStart;

	OpenSimLive::IMUInverseKinematicsToolLive IKTool; // object that calculates IK
	// whether IKTool writes IK orientations into a .mot file when program finishes
	IKTool.setReportErrors(vm.saveIKResults);
	// whether PointTracker (through IKTool) writes calculated mirrored positions and rotations into a .sto file when program finishes
	IKTool.setSavePointTrackerResults(vm.enableMirrorTherapy);

	// get the sensor to opensim rotations for IMUInverseKinematicsToolLive
	SimTK::Vec3 sensorToOpenSimRotations = get_sensor_to_opensim_rotations();

	// SOCKET COMMUNICATION
	bool bResult = false; // tells us if creating the server object succeeded or not
	int port = std::stoi(ConfigReader("MainConfiguration.xml", "socket_port"));
	int dataport = -1; // datagram port, not in use
	// try to create a new server object
	Server myLink(port, dataport, &bResult);
	if (!bResult)
	{
		printf("Failed to create Server object!\n");
	}
	if (vm.enableMirrorTherapy) {
		std::cout << "Waiting for client program to connect..." << std::endl;
		myLink.Connect(); // create socket connection
		std::cout << "Client program connected." << std::endl;
	}



	std::cout << "Entering data streaming and IK loop. Press C to calibrate model, Z to calculate IK once, N to enter continuous mode, M to exit continuous mode, V to enter send mode, B to exit send mode, L to save base reference orientation and X to quit." << std::endl;

	do
	{
		// get IMU orientation data in quaternions
		quaternionData = xsensDataReader.GetQuaternionData(quaternionData);
		if (vm.print_roll_pitch_yaw)
			eulerData = xsensDataReader.GetEulerData(eulerData);
		// update the boolean value to see if new data is available since orientation data was last retrieved
		bool newDataAvailable = xsensDataReader.GetNewDataAvailable();

		// if user hits the key to save the current orientation of the station reference body IMU when it is placed against the mounting surface of the robot arm
		if (referenceBaseRotationKeyHit)
		{
			std::cout << "Setting reference base rotation..." << std::endl;
			if (vm.stationReferenceBody == "none") // if station_reference_body has not been defined in the XML configuration file
			{
				std::cout << "Reference base rotation cannot be calculated because station reference body has not been defined in XML configuration!" << std::endl;
				break;
			}
			OpenSim::TimeSeriesTable_<SimTK::Quaternion>  quaternionTimeSeriesTable(fillQuaternionTable(xsensDataReader.GetMtwCallbacks(), quaternionData));
			bool foundReferenceBodyIMUOrientation = false;
			for (unsigned int i = 0; i < quaternionTimeSeriesTable.getNumColumns(); ++i) // iterate through the quaternion time series table to find the desired IMU data
			{
				if (vm.stationReferenceBody + "_imu" == quaternionTimeSeriesTable.getColumnLabel(i))
				{
					SimTK::Quaternion_<SimTK::Real> quat = quaternionTimeSeriesTable.getMatrixBlock(0, i, 1, 1)[0][0];
					std::cout << "Captured reference body IMU orientation in quaternions: " << quat << std::endl;
					// NEXT: pass it on to PointTracker, or IMUIKTool that inherits PointTracker, and use it at the end of PointTracker rotation calculations
					IKTool.setReferenceBaseRotation(quat);
					IKTool.setUseReferenceRotation(true);
					foundReferenceBodyIMUOrientation = true;
					break;
				}
			}
			if (!foundReferenceBodyIMUOrientation)
				std::cout << "Orientation not found! Make sure an IMU is measuring the orientation of station_reference_body." << std::endl;
			referenceBaseRotationKeyHit = false;
		}

		// if user hits the calibration key and new data is available
		if (newDataAvailable && calibrateModelKeyHit) {
			// set clock to start from calibration
			vm.clockStart = std::chrono::high_resolution_clock::now();
			// fill a timeseriestable with quaternion orientations of IMUs
			OpenSim::TimeSeriesTable_<SimTK::Quaternion>  quaternionTimeSeriesTable(fillQuaternionTable(xsensDataReader.GetMtwCallbacks(), quaternionData));
			// calibrate the model and return its file name
			vm.calibratedModelFile = calibrateModelFromSetupFile(OPENSIMLIVE_ROOT + "/Config/" + ConfigReader("MainConfiguration.xml", "imu_placer_setup_file"), quaternionTimeSeriesTable);
			// reset the keyhit so that we won't re-enter this if-statement before hitting the key again
			calibrateModelKeyHit = false;
			// give IKTool the necessary inputs and run it
			IKTool.setModelFile(vm.calibratedModelFile); // the model to perform IK on
			IKTool.setQuaternion(quaternionTimeSeriesTable); // the orientations of IMUs
			IKTool.setSensorToOpenSimRotations(sensorToOpenSimRotations);
			IKTool.setTime(0); // set the time of the first state as 0 at calibration
			IKTool.setOpenSimLiveRootDirectory(OPENSIMLIVE_ROOT); // this is needed for saving the IK report to file
			// set private variables to be accessed in IK calculations
			if (vm.enableMirrorTherapy == true) {
				IKTool.setPointTrackerBodyName(ConfigReader("MainConfiguration.xml", "station_parent_body"));
				IKTool.setPointTrackerReferenceBodyName(ConfigReader("MainConfiguration.xml", "station_reference_body"));
			}
			else {
				IKTool.setPointTrackerEnabled(false);
			}
			IKTool.run(true); // true for visualization
			std::cout << "Model has been calibrated." << std::endl;


		}

		// if user hits the single IK calculation key, new data is available and the model has been calibrated
		if (newDataAvailable && getDataKeyHit && !vm.calibratedModelFile.empty())
		{
			// use high resolution clock to count time since the IMU measurement began
			vm.clockNow = std::chrono::high_resolution_clock::now();
			vm.clockDuration = vm.clockNow - vm.clockStart; // time since calibration
			RunIKProcedure(xsensDataReader, quaternionData, IKTool, eulerData, myLink, threadPoolContainer, vm);
			getDataKeyHit = false;
		}

		// if new data is available and continuous mode has been switched on
		if (newDataAvailable && vm.continuousMode) {
			// use high resolution clock to count time since the IMU measurement began
			vm.clockNow = std::chrono::high_resolution_clock::now();
			// calculate the duration since the beginning of counting
			vm.clockDuration = vm.clockNow - vm.clockStart;
			// calculate the duration since the previous time IK was continuously calculated
			vm.prevDuration = vm.clockNow - vm.clockPrev;
			// if more than the set time delay has passed since the last time IK was calculated
			if (vm.prevDuration.count() * 1000 > vm.continuousModeMsDelay) {
				// set current time as the time IK was previously calculated for the following iterations of the while-loop
				vm.clockPrev = vm.clockNow;
				RunIKProcedure(xsensDataReader, quaternionData, IKTool, eulerData, myLink, threadPoolContainer, vm);
			}
		}

		if (!vm.continuousMode && startContinuousModeKeyHit && !vm.calibratedModelFile.empty()) {
			std::cout << "Entering continuous mode." << std::endl;
			vm.continuousMode = true;
			startContinuousModeKeyHit = false;
			if (vm.resetClockOnContinuousMode && !(vm.clockDuration.count() > 0)) // ensure that the config setting is set to true and that this is the first time continuous mode is entered
				vm.clockStart = std::chrono::high_resolution_clock::now();
		}

		if (vm.continuousMode && stopContinuousModeKeyHit) {
			std::cout << "Exiting continuous mode." << std::endl;
			vm.continuousMode = false;
			stopContinuousModeKeyHit = false;
		}

		if (!vm.sendMode && startSendModeKeyHit) {
			std::cout << "Starting send mode." << std::endl;
			vm.sendMode = true;
			startSendModeKeyHit = false;
		}

		if (vm.sendMode && stopSendModeKeyHit) {
			std::cout << "Exiting send mode." << std::endl;
			vm.sendMode = false;
			stopSendModeKeyHit = false;
		}

		char hitKey = ' ';
		if (_kbhit())
		{
			hitKey = toupper((char)_getch());
			vm.mainDataLoop = (hitKey != 'X'); // stay in main data loop as long as we don't hit X
			getDataKeyHit = (hitKey == 'Z');
			calibrateModelKeyHit = (hitKey == 'C');
			startContinuousModeKeyHit = (hitKey == 'N');
			stopContinuousModeKeyHit = (hitKey == 'M');
			startSendModeKeyHit = (hitKey == 'V');
			stopSendModeKeyHit = (hitKey == 'B');
			referenceBaseRotationKeyHit = (hitKey == 'L');
		}

	} while (vm.mainDataLoop);

	std::cout << "Exiting main data loop!" << std::endl;

	// when exiting, close socket communication
	if (vm.enableMirrorTherapy) {
		std::cout << "Socket server closing down, sending final packet to disable client..." << std::endl;
		// send a packet that exceeds the limits of the rotation values, indicating this cannot be a legitimate packet and shutting client down on the Java side
		double socketShutdownArray[6] = { 500, 500, 500, 500, 500, 500 };
		myLink.SendDoubles(socketShutdownArray, 6);
		// close socket connection
		myLink.Close();
	}

	// when exiting, save acquired data to file
	if (IKTool.get_report_errors())
	{
		std::cout << "Reporting IK to file..." << std::endl;
		IKTool.reportToFile();
	}

	// close the connection to IMUs
	xsensDataReader.CloseConnection();

		
	std::cout << "Program finished." << std::endl;
	return 1;

}

