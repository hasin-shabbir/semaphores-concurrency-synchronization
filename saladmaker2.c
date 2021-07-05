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

//THIS CHEF ALWAYS HAS GREEN PEPPERS

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
# define chefSemaphore "/chefSem"
# define saladmaker2_Semaphore "/sm2" 
# define mutex "/mutex"
# define wait2_Semaphore "/waitsm2"

//size of shared memory
# define SEGMENTSIZE sizeof(tray)
//permissions
# define PERMCODE 0666

float RandomFloat(float a, float b); // to generate a random floating point number between a and b
float sumWeights(float a,float b,float c); //calculate sum of veg weights

int main(int argc , char ** argv){
    //check missing args
    if (argc<5){
		printf("Arguments missing!\n");
		exit(1);
	}
	srand(time(NULL));
	//read arguments
	int shmid=-1;
	float smTime=-1.0;
	for (int i=1;i<argc;i++){
		if (strcmp(argv[i],"-s")==0){
			shmid=atoi(argv[i+1]);
		}
		else if(strcmp(argv[i],"-m")==0){
			smTime=((float)atoi(argv[i+1]))*RandomFloat(0.8,1);
		}
	}
	if (shmid==-1){
		printf("Shared memory id not provided to saladmaker 2\n");
		exit(1);
	}
	if (smTime<0){
		printf("Invalid saladmaker time provided to saladmaker 2\n");
		exit(1);
	}
	//attach shared memory using the shared memory id
	tray* shmPtr;
	shmPtr = shmat (shmid ,NULL, 0);
	if(shmPtr==(tray*)-1){
		perror("shared memory attachment error in saladmaker 2\n");
		exit(2);
	}
	//open semaphores created by chef
	sem_t *chefSemaphorePtr = sem_open(chefSemaphore, 0);
    if(chefSemaphorePtr == SEM_FAILED){
        perror("chef semaphore not opened in saladmaker 2\n");
        exit(3);
    }
    sem_t *sm2Ptr = sem_open(saladmaker2_Semaphore, 0);
    if(sm2Ptr == SEM_FAILED){
        perror("saladmaker2 semaphore not opened in saladmaker 2\n");
        exit(3);
    }
	sem_t *mutexPtr = sem_open(mutex, 1);
    if(mutexPtr == SEM_FAILED){
		perror("mutex semaphore not opened in saladmaker 2\n");
        exit(5);
    }
	sem_t *waitsm2Ptr = sem_open(wait2_Semaphore, 0);
    if(waitsm2Ptr == SEM_FAILED){
		perror("waitsm2 semaphore not opened in saladmaker 2\n");
        exit(5);
    }

	FILE *fPtr;
	FILE *fPtr2;
	//open logfile for append and check for error in opening
	fPtr = fopen("logfile.txt", "a");
	if (fPtr == NULL)
    {
        /* Unable to open file hence exit */
        printf("\nUnable to open logfile in saladmaker2.\n");
        exit(EXIT_FAILURE);
    }
	fclose(fPtr);

	//initialize data
    float tomatoWeight = 0.0;
    float greenpepperWeight = 0.0;
	float onionWeight =	0.0;
	float totalWeight =	0.0;
	//variables to store working and waiting times
	struct timeval startWork,endWork,startWait,endWait,now;
	//total wait and work time
	float totalWait=0.0;
	float totalWork=0.0;

	while(true){
		//get start of wait time
		gettimeofday(&startWait,NULL);
		fPtr = fopen("logfile.txt", "a");
		//log the difference between start of wait and start of chef
		fprintf(fPtr,"Saladmaker2 started waiting at time: %f\n",(startWait.tv_sec-shmPtr->chefInitTime.tv_sec)+(1e-6)*(startWait.tv_usec-shmPtr->chefInitTime.tv_usec));
		fclose(fPtr);
		//wait for chef
		sem_wait(sm2Ptr);
		//get end of wait time
		gettimeofday(&endWait,NULL);
		fPtr = fopen("logfile.txt", "a");
		//log the difference between end of wait and start of chef
		fprintf(fPtr,"Saladmaker2 ended waiting at time: %f\n",(endWait.tv_sec-shmPtr->chefInitTime.tv_sec)+(1e-6)*(endWait.tv_usec-shmPtr->chefInitTime.tv_usec));
		fclose(fPtr);
		//increment the number of active saladmakers
		shmPtr->activeNum++;

		//as soon as more than 1 saladmakers are active and the previous overlap interval has ended
		//an overlap exists
		if (shmPtr->activeNum==2 && shmPtr->overlapEnd){
			//set overlap end to false
			shmPtr->overlapEnd=false;
			//set overlap start to true
			shmPtr->overlapStart=true;
			//note the time of start of overlap as the end of waiting time
			shmPtr->startOverlap.tv_sec=endWait.tv_sec;
			shmPtr->startOverlap.tv_usec=endWait.tv_usec;
		}
		//calculate total waiting time
		totalWait+=(endWait.tv_sec-startWait.tv_sec)+(1e-6)*(endWait.tv_usec-startWait.tv_usec);

		//mutex for access and modification
		sem_wait(mutexPtr);
		if (shmPtr->task_done){
			//store weights
			shmPtr->weightList[1].tomatoW=tomatoWeight;
			shmPtr->weightList[1].onionW=onionWeight;
			shmPtr->weightList[1].greenPepperW=greenpepperWeight;
			shmPtr->weightList[1].totalW=totalWeight;
			//store waiting and working times
			shmPtr->waitingTime[1]=totalWait;
			shmPtr->workingTime[1]=totalWork;
			//release mutex lock
			sem_post(mutexPtr);
			//allow chef to go through indicating end of saladmaker
			sem_post(waitsm2Ptr);
			break;
		}
		//release mutex
		sem_post(mutexPtr);

		//get time of start of work
		gettimeofday(&startWork,NULL);
		fPtr = fopen("logfile.txt", "a");
		//log the difference between start of work and start of chef
		fprintf(fPtr,"Saladmaker2 started making salad at time: %f\n",(startWork.tv_sec-shmPtr->chefInitTime.tv_sec)+(1e-6)*(startWork.tv_usec-shmPtr->chefInitTime.tv_usec));
		fclose(fPtr);
		printf("\nVegetables received by saladmaker 2, saladmaking in process\n");
		//allow chef to proceed indicating that the vegetables have been picked up
		sem_post(chefSemaphorePtr);

		//sleep or working time of saladmaker
		sleep(smTime);
		//get time of end of work
		gettimeofday(&endWork,NULL);
		fPtr = fopen("logfile.txt", "a");
		//log the difference between end of work and start of chef
		fprintf(fPtr,"Saladmaker2 ended making salad at time: %f\n",(endWork.tv_sec-shmPtr->chefInitTime.tv_sec)+(1e-6)*(endWork.tv_usec-shmPtr->chefInitTime.tv_usec));
		fclose(fPtr);
		//indicate that a salad has been made
		printf("Saladmaker 2 has made a salad\n");
		//update total work time
		totalWork+=(endWork.tv_sec-startWork.tv_sec)+(1e-6)*(endWork.tv_usec-startWork.tv_usec);
		//decrement number of active saladmakers
		shmPtr->activeNum--;
		//as soon as only 1 saladmaker remains active and and overlap was started at some point,
		//end the overlap
		if (shmPtr->activeNum==1 && shmPtr->overlapStart){
			//set overlap as ended
			shmPtr->overlapEnd=true;
			//set overlapstart as false
			shmPtr->overlapStart=false;
			//note time of end of overlap as end of worktime
			shmPtr->endOverlap.tv_sec=endWork.tv_sec;
			shmPtr->endOverlap.tv_usec=endWork.tv_usec;
			fPtr2 = fopen("overlap.txt", "a");
			//log the overlap interval
			fprintf(fPtr2,"Overlap occured between: %f & %f\n",(shmPtr->startOverlap.tv_sec-shmPtr->chefInitTime.tv_sec)+(1e-6)*(shmPtr->startOverlap.tv_usec-shmPtr->chefInitTime.tv_usec),(shmPtr->endOverlap.tv_sec-shmPtr->chefInitTime.tv_sec)+(1e-6)*(shmPtr->endOverlap.tv_usec-shmPtr->chefInitTime.tv_usec));
			fclose(fPtr2);
		}

		//calculate and store vegetable weights
		greenpepperWeight+=80*RandomFloat(0.8,1.2);
		if (strcmp(shmPtr->ing1.veg,"onion")==0){
			onionWeight += shmPtr->ing1.weight;
			tomatoWeight += shmPtr->ing2.weight;
		}
		else{
			onionWeight += shmPtr->ing2.weight;
			tomatoWeight += shmPtr->ing1.weight;
		}
		//update total weight and report as such
		totalWeight=sumWeights(onionWeight,greenpepperWeight,tomatoWeight);
		printf("tomato weight: %0.2f\ngreenpepper weight: %0.2f\nonion weight: %0.2f\ntotal weight: %0.2f\n",tomatoWeight,greenpepperWeight,onionWeight,totalWeight);

		//sem_post(chefSemaphorePtr);
	}

	//close the semaphores
	sem_close(chefSemaphorePtr);
	sem_close(sm2Ptr);

	//detach shared memory
	if(shmdt(shmPtr)==-1){
		perror("Shared memory detachment error in saladmaker 1\n");
		exit(4);
	}
	//end
	return 0;
}
//function to generate random float between a and b
float RandomFloat(float a, float b) {
    float random = ((float) rand()) / (float) RAND_MAX;
    float diff = b - a;
    float r = random * diff;
    return a + r;
}
//sum the weights provided
float sumWeights(float a,float b,float c){
	return a+b+c;
}