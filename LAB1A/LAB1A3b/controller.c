#include <stdio.h>

#define WHOAMI	"CONTROLLER"

#include "interface.h"

static struct input *inputs;
static struct input_interface *ii;
struct timespec t0; //for PRINTF & LOG

static void sigint_handler(int sig __attribute__((unused)))
{
	PRINTF("SIGINT: ending simulation\n");
	ii->simulation_stop = 1;
}

int main()
{
	clock_gettime(CLOCK_MONOTONIC, &t0);

	int nr_inputs = load_inputs(&inputs, "inputs.txt");

	ii = connect_to_input_interface(nr_inputs, 0);
	if (!ii) {
		PRINTF("ERROR: Controller failed to connect to interface! Is input-simulator started?\n");
		exit(1);
	}
	ii->pid_contr = getpid();

	struct sigaction act;
	act.sa_handler = sigint_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGINT, &act, NULL);

	pthread_mutex_lock(&ii->lock);
	ii->controller_ready = 1; //input simulator ready
	
	clock_gettime(CLOCK_MONOTONIC, &t0);
	PRINTF("Controller ready\n");
	
	//wait for input simulator to be ready
	if (ii->input_simulator_ready) {
		//input simulator is ready and waiting for signal
		if (kill(ii->pid_input, SIGUSR1)) {
			perror("kill error");
			exit(1);
		}
	}
	else {
		pthread_mutex_unlock(&ii->lock);
		//wait for input simulator to signal us
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
	
	clock_gettime(CLOCK_MONOTONIC, &t0);
	PRINTF("Controller starting\n");

	//main loop
	while (ii->simulation_stop == 0) {
		int inputs_handled = 0;
		for (int i = 0; i < nr_inputs && ii->simulation_stop == 0; i++) {
			//check/update i-th input
			
			//TODO
			// nije bilo promjene na ulazu i pa SKIP
			if( ii->input[i].request <= ii->input[i].response)
				continue;
			
			inputs_handled++;	
			clock_gettime(CLOCK_MONOTONIC, &(ii->input[i].t_started));
			PRINTF("%s ulaz: (zahtjev %d)\n",inputs[i].name,ii->input[i].request);

			//obrada C
			// nije shareano
			int time_C = inputs[i].C;
			if(ii->input[i].request % 3 == 0) time_C *=2;
			pthread_mutex_unlock(&ii->lock);
			msleep(time_C);
			pthread_mutex_lock(&ii->lock);

			// gotova obrada
			clock_gettime(CLOCK_MONOTONIC,&ii->input[i].t_resp);
			ii->input[i].response++;
			if(ii->input[i].request % 3 == 0) time_C /= 2;
			PRINTF(" %s ulaz : gotova obrada\n",inputs[i].name);
		}

		if (inputs_handled == 0 && ii->simulation_stop == 0) { //if no requests, sleep 1 msec
			pthread_mutex_unlock(&ii->lock);
			msleep(1);
			pthread_mutex_lock(&ii->lock);
		}
	}

	PRINTF("Controller ending\n");
	pthread_mutex_unlock(&ii->lock);

	return 0;
}