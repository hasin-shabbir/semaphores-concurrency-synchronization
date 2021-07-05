#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdbool.h>

//struct for ingredient to be passed
typedef struct{
	char veg[13];
	float weight;
} ingredient;

//struct for weights of ingredients used
typedef struct{
	float tomatoW;
	float greenPepperW;
	float onionW;
	float totalW;
} weights;

//struct for content of shared memory
typedef struct{
	int currSalMak;
	bool task_done;
	ingredient ing1;
	ingredient ing2;
	weights weightList[3];
	float workingTime[3];
	float waitingTime[3];
	struct timeval chefInitTime;
	int activeNum;
	struct timeval startOverlap;
	struct timeval endOverlap;
	bool overlapStart;
	bool overlapEnd;

} tray;

//semaphores to be used
# define chefSemaphore "/chefSem" //for chef
# define saladmaker1_Semaphore "/sm1" //for saladmakers 1 to 3
# define saladmaker2_Semaphore "/sm2" 
# define saladmaker3_Semaphore "/sm3" 
# define mutex "/mutex" //for accessing and modifying data
# define wait1_Semaphore "/waitsm1" //for waiting for saladmakers to end execution before chef can do so
# define wait2_Semaphore "/waitsm2"
# define wait3_Semaphore "/waitsm3"

//size of shared memory
# define SEGMENTSIZE sizeof(tray)
//permissions
# define PERMCODE 0666

//number of saladmaker programs
# define NUMSALADMAKERS 3

void unlinkSems(); //to unlink semaphores
float RandomFloat(float a, float b); // to generate a random floating point number between a and b
void assignWeight(tray*); //assign random weight to each vegetable in the tray
void printLog(); //print the temporal log
void printOverlap(); //print intervals with overlap (parallel running)

int main(int argc , char* argv[]){
	//check missing args
    if (argc<5){
		printf("Arguments missing!\n");
		exit(1);
	}
	srand(time(NULL));
	//read arguments
	int numSalads;
	float chefTime;
	for (int i=1;i<argc;i++){
		if (strcmp(argv[i],"-n")==0){
			numSalads=atoi(argv[i+1]);
			if (numSalads<1){
				printf("No salads need to be produced\n");
				exit(1);
			}
		}
		else if(strcmp(argv[i],"-m")==0){
			chefTime=((float)(atoi(argv[i+1])))*RandomFloat(0.5,1);
		}
	}

	//initial setup
	int shmid; //shared mem id
	tray* shmPtr; //shared mem pointer

	//generate shared mem key
	key_t key = ftok("/tmp", 65);
	if (key == -1){
		perror("key creation error in chef\n");
		exit(2);
	}

	//get shared mem and store id
	shmid = shmget(key, sizeof(tray), IPC_CREAT | PERMCODE);
    if (shmid<0){
        perror("shared memory creation error in chef\n");
        exit(3);
    }
	printf("Shared memory ID: %d\n",shmid);
	//attach shared mem
	shmPtr=(tray *) shmat(shmid,NULL,0);
	if(shmPtr==(tray*)-1){
		perror("shared memory attachment error in chef\n");
		exit(4);
	}

	//unlink semaphores in case they already exist
	unlinkSems();

	//open semaphores for use
	sem_t *chefSemaphorePtr = sem_open(chefSemaphore, O_CREAT, PERMCODE, 0);
    if(chefSemaphorePtr == SEM_FAILED){
        perror("chef semaphore not opened in chef\n");
        exit(5);
    }
    
    sem_t *sm1Ptr = sem_open(saladmaker1_Semaphore,O_CREAT, PERMCODE, 0);
    if(sm1Ptr == SEM_FAILED){
        perror("saladmaker1 semaphore not opened in chef\n");
        exit(5);
    }
    
    sem_t *sm2Ptr = sem_open(saladmaker2_Semaphore, O_CREAT, PERMCODE, 0);
    if(sm2Ptr == SEM_FAILED){
        perror("saladmaker2 semaphore not opened in chef\n");
        exit(5);
    }
    
    sem_t *sm3Ptr = sem_open(saladmaker3_Semaphore, O_CREAT, PERMCODE, 0);
    if(sm3Ptr == SEM_FAILED){
		perror("saladmaker3 semaphore not opened in chef\n");
        exit(5);
    }

	sem_t *mutexPtr = sem_open(mutex, O_CREAT, PERMCODE, 1);
    if(mutexPtr == SEM_FAILED){
		perror("mutex semaphore not opened in chef\n");
        exit(5);
    }

	sem_t *waitsm1Ptr = sem_open(wait1_Semaphore, O_CREAT, PERMCODE, 0);
    if(waitsm1Ptr == SEM_FAILED){
		perror("waitsm1 semaphore not opened in chef\n");
        exit(5);
    }
	sem_t *waitsm2Ptr = sem_open(wait2_Semaphore, O_CREAT, PERMCODE, 0);
    if(waitsm1Ptr == SEM_FAILED){
		perror("waitsm2 semaphore not opened in chef\n");
        exit(5);
    }
	sem_t *waitsm3Ptr = sem_open(wait3_Semaphore, O_CREAT, PERMCODE, 0);
    if(waitsm1Ptr == SEM_FAILED){
		perror("waitsm3 semaphore not opened in chef\n");
        exit(5);
    }

	//initialize data
	int numSaladsMade=0;
	char availableVegs[3][13]={"onion","greenpepper","tomato"};
	int currSM=0; //current saladmaker
	bool useSaladMaker[3]={true,true,true}; //array to choose saladmaker
	int choiceInd; //index to choose saladmaker

	//time of starting of chef program
	gettimeofday(&(shmPtr->chefInitTime),NULL);
	
	//clear the content of any existing log file
	FILE *fPtr;
	fPtr = fopen("logfile.txt", "w");
	if (fPtr == NULL)
    {
        /* Unable to open file hence exit */
        printf("\nUnable to open logfile in chef.\n");
        exit(EXIT_FAILURE);
    }
	fclose(fPtr);

	//clear the content of any existing file with overlap intervals
	FILE *fPtr2;
	fPtr2 = fopen("overlap.txt", "w");
	if (fPtr == NULL)
    {
        /* Unable to open file hence exit */
        printf("\nUnable to open overlap file in chef.\n");
        exit(EXIT_FAILURE);
    }
	fclose(fPtr2);

	//initialize shared memory data
	shmPtr->activeNum=0; //no active saladmakers yet
	shmPtr->overlapEnd=true; //overlap is ended since no overlap exists right now
	shmPtr->overlapStart=false; //no overlap has started at start of execution of chef
	shmPtr->task_done=false; //the task is not complete yet
	//weights of vegetables passed and waiting and working times are all zero:
	for (int i=0;i<3;i++){
		shmPtr->weightList[i].tomatoW=0.0;
		shmPtr->weightList[i].greenPepperW=0.0;
		shmPtr->weightList[i].onionW=0.0;
		shmPtr->weightList[i].totalW=0.0;
		shmPtr->waitingTime[i]=0.0;
		shmPtr->workingTime[i]=0.0;
	}

	//while the required number of salads has not been made
	while(numSaladsMade!=numSalads){
		//all saladmakers are available
		for (int i=0;i<3;i++){
			useSaladMaker[i]=true;
		}
		//pick the first veg
		choiceInd=rand() % NUMSALADMAKERS;
		strcpy(shmPtr->ing1.veg,availableVegs[choiceInd]);
		//saladmaker that already has this vegetable will not be choosen
		useSaladMaker[choiceInd]=false;
		//pick second veg such that it is not the same as the first one
		choiceInd=rand() % NUMSALADMAKERS;
		strcpy(shmPtr->ing2.veg,availableVegs[choiceInd]);
		useSaladMaker[choiceInd]=false;
		while(strcmp(shmPtr->ing1.veg,shmPtr->ing2.veg)==0){
			choiceInd=rand() % NUMSALADMAKERS;
			strcpy(shmPtr->ing2.veg,availableVegs[choiceInd]);
		}
		//saladmaker that already has the second vegetable will not be choosen
		useSaladMaker[choiceInd]=false;

		//calculate weight of vegetables to be sent
		assignWeight(shmPtr);
		//find the saladmaker selected
		for (int i=0;i<NUMSALADMAKERS;i++){
			if (useSaladMaker[i]){
				currSM=i+1;
				break;
			}
		}
		//log and store the current saladmaker number
		printf("\nCurrently saladmaker %d is selected\n",currSM);
		shmPtr->currSalMak=currSM;

		printf("Chef has picked up the vegetables\n");

		//signal the saladmaker selected to proceed
		if (currSM==1){
			sem_post(sm1Ptr);
		}
		if (currSM==2){
			sem_post(sm2Ptr);
		}
		if (currSM==3){
			sem_post(sm3Ptr);
		}
		//as a saladmaker proceeds, the number of salads made is incremented
		numSaladsMade+=1;

		//if no salads are left to be made
		if (numSalads-numSaladsMade==0){
			//wait on the chef semaphore
			sem_wait(chefSemaphorePtr);

			//printf("Salads currently made: %d\n",numSaladsMade);
			
			//label task as completed by accessing via mutex
			sem_wait(mutexPtr);
			shmPtr->task_done=true;
			sem_post(mutexPtr);
			//allow saladmakers to end execution before exiting the loop
			sem_post(sm1Ptr);
			sem_post(sm2Ptr);
			sem_post(sm3Ptr);
			break;
		}
		else{
			//if salads are still to be made, wait for a vegetables to be picked by saladmaker
			sem_wait(chefSemaphorePtr); 
			//chef's rest time
			sleep(chefTime);

		}

	}

	//wait for saladmakers to end execution
	sem_wait(waitsm1Ptr);
	sem_wait(waitsm2Ptr);
	sem_wait(waitsm3Ptr);
	//close the semaphores
	sem_close(chefSemaphorePtr);
	sem_close(sm1Ptr);
	sem_close(sm2Ptr);
	sem_close(sm3Ptr);
	sem_close(mutexPtr);
	sem_close(waitsm1Ptr);
	sem_close(waitsm2Ptr);
	sem_close(waitsm3Ptr);
	printf("\nSemaphores closed in chef\n");

	printf("\nSalad making completed\n");


	//indicate completion of task
	printf("\nTASK COMPLETED SUCCESSFULLY\n");
	printf("--------------------------------------------------------------------");
	printf("\n\nSTATISTICS:\n\n");
	//print number of salads made
	printf("Total salads produced: %d\n",numSaladsMade);

	//print weight of vegetables used by each saladmaker
	for (int i=0;i<3;i++){
		printf("\n\nSum weights for saladmaker %d:\n",i+1);
		printf("\tTomato weight:%0.2f",shmPtr->weightList[i].tomatoW);
		printf("\tGreen Pepper weight:%0.2f",shmPtr->weightList[i].greenPepperW);
		printf("\tOnion weight:%0.2f",shmPtr->weightList[i].onionW);
		printf("\tTotal weight:%0.2f",shmPtr->weightList[i].totalW);
	}

	//print the working time of each saladmaker
	printf("\n\nTotal working time for saladmakers:\n");
	for (int i=0;i<3;i++){
		printf("\tSaladmaker %d:%0.2fs",i+1,shmPtr->workingTime[i]);
	}
	//print the waiting time of each saladmaker
	printf("\n\nTotal waiting time for saladmakers:\n");
	for (int i=0;i<3;i++){
		printf("\tSaladmaker %d:%0.2fs",i+1,shmPtr->waitingTime[i]);
	}
	//print the log file
	printLog();
	//print the overlapping intervals between saladmakers
	printOverlap();

	printf("\n");
	//detach shared memory
	if(shmdt(shmPtr)==-1){
		perror("\nShared memory detachment error in chef\n");
		exit(6);
	}
	//free shared memory
	if (shmctl(shmid,IPC_RMID,NULL)==-1){
		perror("\nShared memory freeing error in chef\n");
		exit(7);
	}

	return 0;
}

//unlink all semaphores
void unlinkSems(){
	sem_unlink(wait1_Semaphore);
	sem_unlink(wait2_Semaphore);
	sem_unlink(wait3_Semaphore);
	sem_unlink(chefSemaphore);
	sem_unlink(saladmaker1_Semaphore);
	sem_unlink(saladmaker2_Semaphore);
	sem_unlink(saladmaker3_Semaphore);
	sem_unlink(mutex);
}
//generate random floating point number between a and b
float RandomFloat(float a, float b) {
    float random = ((float) rand()) / (float) RAND_MAX;
    float diff = b - a;
    float r = random * diff;
    return a + r;
}
//assign weight to vegetables in the tray
void assignWeight(tray* e){
	if (strcmp(e->ing1.veg,"onion")==0){
		e->ing1.weight=60*RandomFloat(0.8,1.2);
	}
	else if (strcmp(e->ing1.veg,"greenpepper")==0){
		e->ing1.weight=80*RandomFloat(0.8,1.2);
	}
	else if (strcmp(e->ing1.veg,"tomato")==0){
		e->ing1.weight=100*RandomFloat(0.8,1.2);
	}
	if (strcmp(e->ing2.veg,"onion")==0){
		e->ing2.weight=60*RandomFloat(0.8,1.2);
	}
	else if (strcmp(e->ing2.veg,"greenpepper")==0){
		e->ing2.weight=80*RandomFloat(0.8,1.2);
	}
	else if (strcmp(e->ing2.veg,"tomato")==0){
		e->ing2.weight=100*RandomFloat(0.8,1.2);
	}
}
//print the logfile
void printLog(){
	printf("\n\nTemporal Log:\n");
	FILE *in=fopen("logfile.txt","r");
	char c;
	while((c=fgetc(in))!=EOF)
		putchar(c);
	fclose(in);
}
//print the overlap intervals by reading from file
void printOverlap(){
	printf("\n\nOverlap stats:\n");
	FILE *in=fopen("overlap.txt","r");
	char c;
	while((c=fgetc(in))!=EOF)
		putchar(c);
	fclose(in);
}