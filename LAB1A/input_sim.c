#include <stdio.h>

#define WHOAMI	"INPUTSIM"

#include "interface.h"

//simulated input state
enum input_state {
	INACTIVE, //initially and after being handled
	ACTIVE    //waiting to be handled by controller
};

//run-time data for simulated input
struct input_data {
	enum input_state state; //INACTIVE, ACTIVE
	struct timespec next_run;
	struct timespec next_deadline;
	
	//stats - number of ...
	int nr_wakeups;		//periods in which input could have changed
				//increment at the start of period, regardless if request was handled or not
	int nr_OK_responses;	//periods with responses before deadline
	int nr_LATE_responses;	//periods with late response - both short and long lateness (before next_run and after)
	int nr_NO_responses;	//periods with no response arriving in them

	//stats - times
	//response time: from request till completion
	long response_sum;
	long response_min;
	long response_max;
	
	//reaction time: from request till starting of processing on controller
	long reaction_sum;
	long reaction_min;
	long reaction_max;
};

static struct input *inputs;
static struct input_interface *ii;
static struct input_data *id;
struct timespec t0; //PRINTF

static void sigint_handler(int sig __attribute__((unused)))
{
	PRINTF("SIGINT: ending simulation\n");
	ii->simulation_stop = 1;

}


int main()
{
	clock_gettime(CLOCK_MONOTONIC, &t0);

	int nr_inputs = load_inputs(&inputs, "inputs.txt");

	delete_input_interface(); //remove existing interface, if exists
	ii = connect_to_input_interface(nr_inputs, 1);
	if (!ii) {
		PRINTF("ERROR: Input simulator failed to create interface\n");
		exit(1);
	}
	ii->pid_input = getpid();
	
	struct sigaction act;
	act.sa_handler = sigint_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGINT, &act, NULL);

	id = malloc(sizeof(struct input_data) * nr_inputs);

	pthread_mutex_lock(&ii->lock);
	ii->input_simulator_ready = 1; //input simulator ready
	
	clock_gettime(CLOCK_MONOTONIC, &t0);
	PRINTF("Input simulator ready\n");
	
	//wait for controller to be ready
	if (ii->controller_ready) {
		//controller is ready and waiting for signal
		if (kill(ii->pid_contr, SIGUSR1)) {
			perror("kill error");
			exit(1);
		}
	}
	else {
		pthread_mutex_unlock(&ii->lock);
		//wait for controller to signal us
		sigset_t set;
		sigemptyset(&set);
		sigaddset(&set, SIGUSR1);
		if (pthread_sigmask(SIG_BLOCK, &set, NULL)) {
			perror("pthread_sigmask error");
			exit(1);
		}
		int sig;
		if (sigwait(&set, &sig)) {
			perror("sigwait error");
			exit(1);
		}
		pthread_mutex_lock(&ii->lock);
	}
	
	ii->simulation_start = 1;

	clock_gettime(CLOCK_MONOTONIC, &t0);
	PRINTF("Input simulator starting\n");

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	//initialize input data for all inputs based on "inputs[]"
	for (int i = 0; i < nr_inputs; i++) {
		id[i].state = INACTIVE;
		id[i].nr_wakeups = 0;
		id[i].nr_OK_responses = 0;
		id[i].nr_LATE_responses = 0;
		id[i].nr_NO_responses = 0;

		id[i].response_sum = 0;
		id[i].response_min = LONG_MAX;
		id[i].response_max = 0;

		id[i].reaction_sum = 0;
		id[i].reaction_min = LONG_MAX;
		id[i].reaction_max = 0;

		id[i].next_run = now;
		TIMESPEC_ADD_MS(id[i].next_run, inputs[i].t0);
		
		id[i].next_deadline = id[i].next_run;
		TIMESPEC_ADD_MS(id[i].next_deadline, inputs[i].D);
	}

	//main loop
	while (ii->simulation_stop == 0) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		
		//LOG("Checking all inputs\n");

		for (int i = 0; i < nr_inputs && ii->simulation_stop == 0; i++) {
			//check/update i-th input
			// ERICSSON praksa kod if :)
			if(id[i].state == INACTIVE)
			{
				clock_gettime(CLOCK_MONOTONIC,&now);
				if(TIMESPEC_LE(id[i].next_run,now))
				{
					ii->input[i].request++;
					ii->input[i].t_req = now;
					id[i].nr_wakeups++;
					id[i].state = ACTIVE;
				}
			}
			else
			{
				// odgovor dobiven
				if(ii->input[i].response == ii->input[i].request)
				{
					if(TIMESPEC_LE(ii->input[i].t_resp, id[i].next_deadline))
					{
						id[i].nr_OK_responses++;
						PRINTF("%s ulaz : STIGAO NA VRIJEME\n",inputs[i].name);
					}
					else
					{
						id[i].nr_LATE_responses++;
						PRINTF("%s ulaz : STIGAO PREKASNO\n", inputs[i].name);
					}

					struct timespec temp = ii->input[i].t_resp;
					TIMESPEC_SUB(temp,ii->input[i].t_req);
					long diff=TIMESPEC_TO_MS(temp);

					id[i].response_sum += diff;
					SET_MIN(id[i].response_min, diff);
					SET_MAX(id[i].response_max, diff);

					temp = ii->input[i].t_started;
					TIMESPEC_SUB(temp, ii->input[i].t_req);
					diff = TIMESPEC_TO_MS(temp);

					id[i].reaction_sum += diff;
					SET_MIN(id[i].reaction_min, diff);
					SET_MAX(id[i].reaction_max, diff);

					id[i].state = INACTIVE;
					TIMESPEC_ADD_MS(id[i].next_run, inputs[i].T);
					id[i].next_deadline = id[i].next_run;
					TIMESPEC_ADD_MS(id[i].next_deadline, inputs[i].D);

				}
				// odgovora nema
				else
				{
					clock_gettime(CLOCK_MONOTONIC, &now);
					struct timespec temp = id[i].next_run;
					TIMESPEC_ADD_MS(temp, inputs[i].T);

					if(TIMESPEC_GT(now,temp))
					{
						id[i].nr_NO_responses++;
						id[i].nr_wakeups++;

						PRINTF("ULAZ %s PRESKOCEN\n",inputs[i].name);
						TIMESPEC_ADD_MS(id[i].next_run, inputs[i].T);
					}
				}
			}
			//TODO
		}

		pthread_mutex_unlock(&ii->lock);
		msleep(1);
		pthread_mutex_lock(&ii->lock);
	}
	pthread_mutex_unlock(&ii->lock);
	
	int nr_wakeups = 0;
	int nr_OK_responses = 0;
	int nr_LATE_responses = 0;
	int nr_NO_responses = 0;

	long response_sum = 0;
	long response_min = LONG_MAX;
	long response_max = 0;
	
	long reaction_sum = 0;
	long reaction_min = LONG_MAX;
	long reaction_max = 0;
	
	for (int i = 0; i < nr_inputs; i++) {
		//print stat for i-th input
		printf("\nInput %s (t0=%d, T=%d, C=%d, D=%d):\n", inputs[i].name,
			inputs[i].t0, inputs[i].T, inputs[i].C, inputs[i].D);
		printf("nr_wakeups=%d\n", id[i].nr_wakeups);
		printf("nr_OK_responses=%d\n", id[i].nr_OK_responses);
		printf("nr_LATE_responses=%d\n", id[i].nr_LATE_responses);
		printf("nr_NO_responses=%d\n", id[i].nr_NO_responses);

		printf("response_avg=%ld\n", id[i].response_sum/(id[i].nr_OK_responses + id[i].nr_LATE_responses));
		printf("response_min=%ld\n", id[i].response_min);
		printf("response_max=%ld\n", id[i].response_max);

		printf("reaction_avg=%ld\n", id[i].reaction_sum/(id[i].nr_OK_responses + id[i].nr_LATE_responses));
		printf("reaction_min=%ld\n", id[i].reaction_min);
		printf("reaction_max=%ld\n", id[i].reaction_max);

		nr_wakeups += id[i].nr_wakeups;
		nr_OK_responses += id[i].nr_OK_responses;
		nr_LATE_responses += id[i].nr_LATE_responses;
		nr_NO_responses += id[i].nr_NO_responses;

		response_sum += id[i].response_sum;
		SET_MIN(response_min, id[i].response_min);
		SET_MAX(response_max, id[i].response_max);

		reaction_sum += id[i].reaction_sum;
		SET_MIN(reaction_min, id[i].reaction_min);
		SET_MAX(reaction_max, id[i].reaction_max);
	}

	printf("\nOverall stats:\n");
	printf("nr_wakeups=%d\n", nr_wakeups);
	printf("nr_OK_responses=%d\n", nr_OK_responses);
	printf("nr_LATE_responses=%d\n", nr_LATE_responses);
	printf("nr_NO_responses=%d\n", nr_NO_responses);

	printf("response_avg=%ld\n", response_sum/(nr_OK_responses + nr_LATE_responses));
	printf("response_min=%ld\n", response_min);
	printf("response_max=%ld\n", response_max);

	printf("reaction_avg=%ld\n", reaction_sum/(nr_OK_responses + nr_LATE_responses));
	printf("reaction_min=%ld\n", reaction_min);
	printf("reaction_max=%ld\n", reaction_max);

	return 0;
}