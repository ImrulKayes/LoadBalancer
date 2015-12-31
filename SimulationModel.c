
//----- Includes --------------------------------------------------------------
#include "csim.h"   // Needed for CSIM 20 for C stuff
#include <stdio.h>  // Needed for I/O functions
#include <conio.h>  // Needed to use getch() function to hold the output

//----- Constants -------------------------------------------------------------
#define NUMBER_OF_SERVERS 5     // Number of servers in the system
#define STALE_PERIOD      10.0   // Peroid of updating load balancer about the queue length of servers 
#define MAX_TIME          60.0  // Maximum simulation CPU time in seconds
#define CI_LEVEL          0.95  // Confidence interval level
#define ACCURACY          0.01  // Target accuracy

//------New types--------------------------------------------------------------
enum BALANCER_TYPE  // Type of the load balancer
{
	random,
	roundRobin,
	shortestQueue,
	shortestQueueStale,
	improved
};

//----- Global variables ------------------------------------------------------
FACILITY           ServerFacility[NUMBER_OF_SERVERS];    // Server facilities represent servers in the system
TABLE	           DelayTable;							 // Table to store the waiting time in the system for each customer
enum BALANCER_TYPE LoadBalancer;						 // Chosen load balancer
int                ArrivalCounter;					     // Stores total number of customers arrivals
int				   RoundRobinServerIDCounter = 0;        // Stores counter value for Round Robin load balancer
int				   QueueLength[NUMBER_OF_SERVERS];       // Stores the queue length of each server
double			   PreviosUpdateClock;                   // Stores clock of previous balancer acknowldgement of server queue length
int				   StaleQueueLength[NUMBER_OF_SERVERS];  // Stores queue length of the servers which is not updated

//----- Prototypes ------------------------------------------------------------
void generateCustomers(double lambda, double mu);
void queueServer(int serverID, double serviceTime);
void (* server[NUMBER_OF_SERVERS])(int serverID, double serviceTime, double orgTime);
int chooseBalancerDialog();
int generateRandomInteger(int minValue, int maxValue);
void updateInformation();
int randomLoadBalancer();
int roundRobinLoadBalancer();
int shortestQueueLoadBalancer();
int shortestQueueStaleLoadBalancer();
int improvedLoadBalancer();

//===========================================================================
//=  Main program.                                                          =
//=  This function starts the simulation. First it initializes everything.  =
//=  Then it asks user to choose the load balancing strategy. After the     =
//=  strategy is chosen it starts customers generation and holds for        =
//=  SIMULATION_TIME. In the end it prints all the simulation statistics.   =
//=-------------------------------------------------------------------------=
//=  Inputs: None                                                           =
//=  Returns: None                                                          =
//===========================================================================
void sim()
{
	double lambda;           // Customers arrivale rate
	double mu;               // Service rate for each server in the system    
	double meanResponseTime; // Mean response time of the system
	int    i;                // Loop counter. Service variable. 
	char   processName[15];  // Service char array. Is used to create different name for every process

	// Ask user about load balancing strategy.
	// User choice is stored in the global variable LoadBalancer.
    LoadBalancer = chooseBalancerDialog();

	// Create the simulation process
	create("Sim");

	// Simulation has been started
	printf("\n*** BEGIN SIMULATION *** \n");

	// Initialize global variable DelayTable
	DelayTable = table("Delay Table");
	// Implement run length control
	// Run until we achive the desired ACCURACY with the desired probability
	table_confidence(DelayTable);
	table_run_length(DelayTable, ACCURACY, CI_LEVEL, MAX_TIME);

	// Initialize lambda, mu, ArrivalCounter
	lambda = 3.5;
	mu = 1.0;
	ArrivalCounter = 0;

	// Initialize facilities and queues
	for (i = 0; i < NUMBER_OF_SERVERS; i++)
	{
		// Process name contains the server number (ID)
		sprintf(processName, "Server %d", i);
		ServerFacility[i] = facility(processName);
		// Create queue for each server
		server[i] = queueServer;
	}

	// If we need to start updating the data about servers
	if ((LoadBalancer == shortestQueueStale) || (LoadBalancer == improved))
	{
		updateInformation();
	}

	// Start customers generation
	generateCustomers(lambda, mu);

	// Simulation runs for SIMULATION_TIME
	wait(converged);

	// Calculate additional statistics
	// Calculate the mean response time of the system as an average
	// of the servers response time.
	meanResponseTime = 0.0;
	for (i = 0; i < NUMBER_OF_SERVERS; i++)
	{
		meanResponseTime += resp(ServerFacility[i]);
	}
	meanResponseTime /= NUMBER_OF_SERVERS;

	// Print statistics
	printf("\n");
	report();
	printf("Total arrivals: %d\n", ArrivalCounter);
	printf("Mean response time: %.6f\n", meanResponseTime);

	// End of the simulation
	printf("\n*** END SIMULATION *** \n");

	// Hold statistics on the screen
	getch();
}

//===========================================================================
//=  Function generates new customers and send them to a particular         =
//=  server. Arrival time has Poisson distribution. The server for each     =
//=  customer is determined by the load balancer. User chooses load         =
//=  balancer in the _sim function. This function also counts the total     =
//=  number of customers which have arrived to the system.                  =
//=-------------------------------------------------------------------------=
//=  Inputs: lambda - customers arrival rate                                =
//=          mu     - servers service rate                                  =
//=  Returns: None                                                          =
//===========================================================================
void generateCustomers(double lambda, double mu)
{
	double interarrivalTime;  // Time between each customer arrival
	double serviceTime;       // Service time for each customer
	int    nextServerID;      // ID of the server to queue current customer to

	create("Generate");  // Create the generation process

	// Create customers forever
	while(1)
	{
		// Interarrival time in Poisson process has exponential distribution
		interarrivalTime = exponential(1.0 / lambda);
		// Wait for the next customer
		hold(interarrivalTime);

		// New customer has arrived. Increment ArrivalCounter
		ArrivalCounter++;

		// Service time for the current customer has exponential distribution
		serviceTime = exponential(1.0 / mu);

		// Choose the server for the current customer based on the chosen load balancer
		if (LoadBalancer == random)
		{
			nextServerID = randomLoadBalancer();
		}
		if (LoadBalancer == roundRobin)
		{
			nextServerID = roundRobinLoadBalancer();
		}
		if (LoadBalancer == shortestQueue)
		{
			nextServerID = shortestQueueLoadBalancer();
		}
		if (LoadBalancer == shortestQueueStale)
		{
			nextServerID = shortestQueueStaleLoadBalancer();
		}
		if (LoadBalancer == improved)
		{
			nextServerID = improvedLoadBalancer();
		}
		
		// Queue current customer to the chosen server
		server[nextServerID](nextServerID, serviceTime, clock);
	}
}

//===========================================================================
//=  Single server queue. This function put the customer into the server    =
//=  queue. When time comes, it reserves the required server for the        =
//=  customer and server serves it. When customer has recieved the service, =
//=  the function releases the server and updates the DelayTable            =
//=-------------------------------------------------------------------------=
//=  Inputs: serverID    - ID of the server for the customer                =
//=          serviceTime - time which is required to serve the customer     =
//=          orgTime     - time when the customer was queued                =
//=  Returns: None                                                          =
//===========================================================================
void queueServer(int serverID, double serviceTime, double orgTime)
{
	double responseTime;  // Is used to store the respose time for the customer

	create("Queue Server");

	// Size of the server's queue is 200
	// Error if there is no place in the queue for the current job
	if (qlength(ServerFacility[serverID]) > 199)
	{
		printf("!!! ERROR !!!\n");
		printf("System is broken! Queue overflow on the Server %d\n", serverID + 1);
		getch();

		exit();
	}

	// Reserve server, then wait while the service is provided, the release the server
	reserve(ServerFacility[serverID]);
	hold(serviceTime);
	release(ServerFacility[serverID]);

	// Calculate the response time for the customer
	responseTime = clock - orgTime;

	// Record customer delay in the table for the convergence test
	record(responseTime, DelayTable);
}

//===========================================================================
//=  This function initiates a dialog with the user and asks him what type  =
//=  of the Load Balancer does he want. Allowable types are listed in the   =
//=  enum BALANCER_TYPE. User must press any key from 1 to 5 to choose the  =
//=  load balancer.                                                         =
//=-------------------------------------------------------------------------=
//=  Inputs: None                                                           =
//=  Returns: userChoice - chosen load balancer                             =
//===========================================================================
enum BALANCER_TYPE chooseBalancerDialog()
{
	int userChoice = 0;  // The key pressed by user is stored here

	// Ask user what balancer does he want
	printf("Choose workload balancing strategy for the system. Press:\n");
	printf("  1 - for Random balancing strategy\n");
	printf("  2 - for Round Robin balancing strategy\n");
	printf("  3 - for Up-to-Date Shortest Queue balancing strategy\n");
	printf("  4 - for Stale Shortest Queue balancing strategy\n");
	printf("  5 - for Improved balancing strategy\n");

	// Read the key pressed by the user while the choice is not allowable
	// Allowed choice: integer from 1 to 5
	while ((userChoice < 1) || (userChoice > 5))
	{
		printf("Your choice: ");
		scanf("%d", &userChoice);

		// Error message the choice is bad
		if ((userChoice < 1) || (userChoice > 5))
		{
			printf("ERROR! Your choice is not appropriate. Try again please.\n");
		}
	}

	// "-1" because recods in the enum start from 0 and user choice is from 1 to 4.
	userChoice = userChoice - 1;

	return(userChoice);
}

//===========================================================================
//=  This function generates a random integer between minValue and maxValue =
//=-------------------------------------------------------------------------=
//=  Inputs: minValue - minimum desired integer                             =
//=  Inputs: minValue - maximum desired integer                             =
//=  Returns: randomInteger - generated random integer                      =
//===========================================================================
int generateRandomInteger(int minValue, int maxValue)
{
	double randomNumber;   // Stores generated random double number
	int    randomInteger;  // Stores the random integer which we return

	// Generate a random uniformly distributed double value
	randomNumber = uniform((double) minValue, (double) maxValue + 1);

	// If we were so lucky to hit the maxValue
	if (randomNumber == maxValue)
	{
		randomInteger = maxValue;
	}
	// In general case we just dismiss the fractional part of the generated value
	else
	{
		randomInteger = (int) randomNumber;
	}

	return(randomInteger);
}

//===========================================================================
//=  This function creates the CSIM process which will update the values    =
//=  of the QueueLenth array every STALE_PERIOD time. This function is      =
//=  called from the "sim" function.                                        =
//=-------------------------------------------------------------------------=
//=  Inputs: None                                                           =
//=  Returns: None                                                          =
//===========================================================================
void updateInformation()
{
	int i;  // In]teration counter. Service variable

	// Create the CSIM process
	create("Update");

	while (1)
	{
		// Update information about queue lengths
		for (i = 0; i < NUMBER_OF_SERVERS; i++)
		{
			QueueLength[i] = qlength(ServerFacility[i]) + num_busy(ServerFacility[i]);
		}

		// Wait STALE_PERIOD till next update
		hold(STALE_PERIOD);
	}
}

//===========================================================================
//=  This is a Random Load Balancer. Function decides to which server the   =
//=  customer should be queued based on the random load balancing strategy. =
//=  It generates unifirmly distributed random number and retuns the server =
//=  ID based on it.                                                        =
//=-------------------------------------------------------------------------=
//=  Inputs: None                                                           =
//=  Returns: serverID - ID of the server which is chosen for the customer  =
//===========================================================================
int randomLoadBalancer()
{
	int    serverID;      // Stores ID of the chosen server (simply the number of the server)

	serverID = generateRandomInteger(0, 4);

	return(serverID);
}

//===========================================================================
//=  This is a Round Robin Load Balancer. Function decides to which server  =
//=  the customer should be queued based on the round robin load balancing  =
//=  strategy. The first customer goes to the server 1, the second to the   =
//=  server 2 and so on (the 6-th to the server 1 again). Information       =
//=  about the last choice is stored in RoundRobinServerIDCounter.          =
//=-------------------------------------------------------------------------=
//=  Inputs: None                                                           =
//=  Returns: serverID - ID of the server which is chosen for the customer  =
//===========================================================================
int roundRobinLoadBalancer()
{
	int serverID; // Stores ID of the chosen server (simply the number of the server)
	
	// Generates serverID with the help of RoundRobinServerIDCounter
	serverID = RoundRobinServerIDCounter % NUMBER_OF_SERVERS;
	RoundRobinServerIDCounter++;

	return(serverID);
}

// New try of the shotest queue with up-to-date information
//===========================================================================
//=  This is an Up-to-Date Shortest Queue Load Balancer. Function decides to=
//=  which server the customer should be queued based on the length of the  =
//=  servers queue. The customer goes will be scheduled to the server with  =
//=  the shortest queue length. If several servers have the same queue      =
//=  length which is also the minimum queue length, then the load balancer  =
//=  will randomly choose the server. Load balancer can get an up-to-date   =
//=  infomation about the server's queue every time it makes the decision.  =
//=-------------------------------------------------------------------------=
//=  Inputs: None                                                           =
//=  Returns: serverID - ID of the server which is chosen for the customer  =
//===========================================================================
int shortestQueueLoadBalancer()
{
	int    i;                                     // Iteration counter
	double randomNumberArray[NUMBER_OF_SERVERS];  // An array with generated random number for each server
	int    shortestQueueServerID;                // The ID of the chosen server is stored here

	for (i = 0; i < NUMBER_OF_SERVERS; i++)
	{
		// Get fresh queue length for each server
		QueueLength[i] = qlength(ServerFacility[i]) + num_busy(ServerFacility[i]);
		// Generate a random double from 0 to 1 for each server
		// This number is used to make a desicion when several server have
		// the same queue length and it is the minimum one.
		randomNumberArray[i] = uniform01();
	}

	// Find the server with the shortest queue
	shortestQueueServerID = 0;
	for (i = 1; i < NUMBER_OF_SERVERS; i++)
	{
		// If the queue length of the next server is less the current minimum
		if (QueueLength[i] < QueueLength[shortestQueueServerID])
		{
			shortestQueueServerID = i;
		}
		// Case when several server have minimum queue length
		else
		{
			// Randomly choose the server from the list of server with minimum queue lenth
			// Desicion is based on the random number generate for each server
			// A server with the minimum random number is chosen
			if ((QueueLength[i] == QueueLength[shortestQueueServerID]) &&
				(randomNumberArray[i] < randomNumberArray[shortestQueueServerID]))
			{
				shortestQueueServerID = i;
			}
		}
	}

	return(shortestQueueServerID);
}

// New try of the shortest queue with stale information
//===========================================================================
//=  This is a Stale Shortest Queue Load Balancer. Function decides to      =
//=  which server the customer should be queued based on the length of the  =
//=  servers queue. The customer goes will be scheduled to the server with  =
//=  the shortest queue length. If several servers have the same queue      =
//=  length which is also the minimum queue length, then the load balancer  =
//=  will randomly choose the server. Load balancer cannot get an           =
//=  up-to-date infomation about the server's queue every time it makes the =
//=  decision. Information is updated by the updateInformation function     =
//=  every STALE_PERIOD time.                                               =
//=-------------------------------------------------------------------------=
//=  Inputs: None                                                           =
//=  Returns: serverID - ID of the server which is chosen for the customer  =
//===========================================================================
int shortestQueueStaleLoadBalancer()   
{
	int i;                                       // Iteration counter
	double randomNumberArray[NUMBER_OF_SERVERS]; // An array with generated random number for each server
	int shortestQueueServerID;                   // The ID of the chosen server is stored here

	// Generate a random double from 0 to 1 for each server
	// This number is used to make a desicion when several server have
	// the same queue length and it is the minimum one.
	for (i = 0; i < NUMBER_OF_SERVERS; i++)
	{
		randomNumberArray[i] = uniform01();
	}

	// Find the server with the shortest queue
	shortestQueueServerID = 0;
	for (i = 1; i < NUMBER_OF_SERVERS; i++)
	{
		// If the queue length of the next server is less the current minimum
		if (QueueLength[i] < QueueLength[shortestQueueServerID])
		{
			shortestQueueServerID = i;
		}
		// Case when several server have minimum queue length
		else
		{
			// Randomly choose the server from the list of server with minimum queue lenth
			// Desicion is based on the random number generate for each server
			// A server with the minimum random number is chosen
			if ((QueueLength[i] == QueueLength[shortestQueueServerID]) &&
				(randomNumberArray[i] < randomNumberArray[shortestQueueServerID]))
			{
				shortestQueueServerID = i;
			}
		}
	}

	return(shortestQueueServerID);
}

//===========================================================================
//=  This is an Improved Load Balancer. Function decides to which server    =
//=  the customer should be queued based on the length of the servers queue.=
//=  This load balancer cannot get the up-to-date information about the     =
//=  servers queue length. So, it uses stale information but also increment =
//=  the length of the server's queue each time it schedules the customer   =
//=  for this server. Each time it will choose the server with the shortest =
//=  queue. If several servers have the same queue length which is also the =
//=  length which is also the minimum queue length, then the load balancer  =
//=  minimum will randomly choose the server. Information about the         =
//=  server's queue length is updated by the updateInformation function.    =
//=  This function just writes the new information to the QueueLength array =
//=  deleting the previous information written by the load balancer.        =
//=-------------------------------------------------------------------------=
//=  Inputs: None                                                           =
//=  Returns: serverID - ID of the server which is chosen for the customer  =
//===========================================================================
int improvedLoadBalancer()
{
	int    i;                                     // Iteration counter
	double randomNumberArray[NUMBER_OF_SERVERS];  // An array with generated random number for each server
	int    shortestQueueServerID;                 // The ID of the chosen server is stored here

	// Generate a random double from 0 to 1 for each server
	// This number is used to make a desicion when several server have
	// the same queue length and it is the minimum one.
	for (i = 0; i < NUMBER_OF_SERVERS; i++)
	{
		randomNumberArray[i] = uniform01();
	}

	// Find the server with the shortest queue
	shortestQueueServerID = 0;
	for (i = 1; i < NUMBER_OF_SERVERS; i++)
	{
		// If the queue length of the next server is less the current minimum
		if (QueueLength[i] < QueueLength[shortestQueueServerID])
		{
			shortestQueueServerID = i;
		}
		// Case when several server have minimum queue length
		else
		{
			// Randomly choose the server from the list of server with minimum queue lenth
			// Desicion is based on the random number generate for each server
			// A server with the minimum random number is chosen
			if ((QueueLength[i] == QueueLength[shortestQueueServerID]) &&
				(randomNumberArray[i] < randomNumberArray[shortestQueueServerID]))
			{
				shortestQueueServerID = i;
			}
		}
	}

	// Keep the history by incrementing the length of the server's queue
	// every time we scahedule the custome for this server.
	QueueLength[shortestQueueServerID]++;

	return(shortestQueueServerID);
}
