#include <pthread.h>
#include <queue>
#include <iostream>
#include <algorithm>
#include <unistd.h>
#include <stdlib.h>
#include <vector>
#include <chrono>
#include <iomanip>

#define BURST_TIME 100000

using namespace std;

// Define the request struct
typedef struct {
    int request_id;
    int type; // the type of transaction for this request
    int resource_needed; // resource needed for specific resource
    double arrival_time; 
    double completion_time;
} Request;

// Define the worker thread struct
typedef struct {
    int id; // unique ID for each thread
    int priority; // priority level of the thread
    int max_resource; // maximum number of requests the thread can handle
    int available_resource; // number of requests currently being processed
    queue<Request*> request_queue; // the queue of requests waiting to be processed
    pthread_mutex_t lock; // mutex lock to synchronize access to the thread
    pthread_cond_t cond; // condition variable to signal when a worker thread becomes available
} WorkerThread;

// Define the service struct
typedef struct {
    int type; // the type of transaction this service handles
    int num_threads; // the number of worker threads in the service pool
    vector<WorkerThread> pool; // the pool of worker threads
    pthread_t* threads;
    
} Service;


int servicable_requests;
pthread_mutex_t lock_count; // a type defined in the pthread library for implementing mutual exclusion locks
int executed_requests = 0;
vector<int> order_of_execution; // creating a vector to store the execution order
Service *services;
std::chrono::high_resolution_clock::time_point start_time; //used to measure the duration of an operation or the elapsed time between two points in time

// define a function to find the worker thread with highest priority and available resources
WorkerThread* findWorkerThread(int service_type , Request request) {
    WorkerThread* selected_thread = NULL;  // initalize it with null
    int n = services[service_type].pool.size(); //representing the number of elements in the pool
    for (int i = 0; i<n ;i++) 
    {
        
        pthread_mutex_lock(&(services[service_type].pool[i].lock)); // to acquire a lock 
        
        if(request.resource_needed <= services[service_type].pool[i].available_resource)  // checking the condition for the resource needed for specific request
        {
            selected_thread = &(services[service_type].pool[i]); // it is going to selects the worker thread
            
            services[service_type].pool[i].available_resource -= request.resource_needed; // after selecting the worker thread it is going to reduce the number of available resource
            pthread_mutex_unlock(&(services[service_type].pool[i].lock)); // unlock it
           
            break;
        }
        pthread_mutex_unlock(&(services[service_type].pool[i].lock));
        
    }

    return selected_thread;
}

// The main function for the worker thread
void* worker_thread_func(void* arg) {
    WorkerThread* thread = (WorkerThread*) arg;
   
    while (true) {
        // Wait for a request to be assigned to this thread
        pthread_mutex_lock(&thread->lock);
        while (thread->request_queue.size() <= 0)  // checking if the request queue of the thread is empty
		{
            pthread_cond_wait(&thread->cond, &thread->lock); // it will put the thread to sleep 
            pthread_mutex_lock(&lock_count);
            if(executed_requests == servicable_requests) // if all the request got executed then come out of this loop
            {
                pthread_mutex_unlock(&lock_count);
                break;
            }
            pthread_mutex_unlock(&lock_count);
        }
        pthread_mutex_lock(&lock_count);
        if(thread->request_queue.size() <=0 && executed_requests == servicable_requests) // checking if all request are executed then come out of function
        {
               
            pthread_mutex_unlock(&lock_count);
            break;
        }
        pthread_mutex_unlock(&lock_count);
        Request *request = thread->request_queue.front();
        thread->request_queue.pop();
        cout<<"Processing request : "<<request->request_id<<"\n";
        usleep(BURST_TIME);
        pthread_mutex_lock(&lock_count);
        executed_requests++; // increment the number of executed request
        order_of_execution.push_back(request->request_id); // push the request number in this vector
        std::chrono::high_resolution_clock::time_point request_completion_time = std::chrono::high_resolution_clock::now();  // calculating the completion time for specific request
        std::chrono::microseconds elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(request_completion_time - start_time); // time taken for execution
        request->completion_time = (double)elapsed_time.count()/1e3; // converting it to millisecond
        cout<<"EXECUTED request : "<<request->request_id<<endl;
        pthread_mutex_unlock(&lock_count);
        thread->available_resource += request->resource_needed;// after execution of request it will release the resource and it will added to the available resource
        pthread_cond_signal(&thread->cond);
        pthread_mutex_unlock(&thread->lock);
    }
    return NULL;
}
 
// this function is created for checking that if worker thread is not available then is it possible that it will assign later 
bool check_possibility(int service_type , Request request, vector<WorkerThread>& threads)
{
    int n = services[service_type].pool.size();;
    for (int i = 0; i<n ;i++) 
    {
        pthread_mutex_lock(&(services[service_type].pool[i].lock));
        if(request.resource_needed <= services[service_type].pool[i].max_resource)  
        {
        	pthread_mutex_unlock(&(services[service_type].pool[i].lock));
        	return true; // if the resource we need for specific request is less than max resource
        }
        pthread_mutex_unlock(&(services[service_type].pool[i].lock));
    }
    return false;
}

// this function is to compare between two worker thread
bool compare_priority(WorkerThread a, WorkerThread b)
{
    return a.priority < b.priority;
}

int main() {
     // taking two variable first for number of services and second for number of thread in each service
    int n,m;
    cout<<"Enter the number of services : ";
    cin>>n;
    cout<<endl;
    cout<<"Enter the number of threads in service  : ";
    cin>>m;
    cout<<endl;
    services = new Service[n]; 
    queue<int> blocked_requests,rejected_requests; //creating two queue one to store the request which get blocked due to lack of resource 
	                                                // another for storing request which are rejected due to resource limitation
    for(int i = 0; i<n; i++)
    {
        services[i].type = i; //keep track of the service type associated with each element in the array
        
        cout<<"Enter priority resources for each thread in service "<<i<<"\n";
        services[i].pool.resize(m); //to store the worker threads that are part of the service
        services[i].threads = new pthread_t[m]; //creates an array of m worker threads that will be used to handle incoming requests for the service.
        for(int j = 0; j<m; j++)
        {
            cin>>services[i].pool[j].priority;
            cin>>services[i].pool[j].max_resource; // user to enter the priority and maximum resources for the worker thread 
            
            services[i].pool[j].available_resource = services[i].pool[j].max_resource; // initially all the resource are available
            pthread_mutex_init(&(services[i].pool[j].lock), NULL); // initializes the lock member variable of the worker thread
        }
        sort(services[i].pool.begin(), services[i].pool.end(), compare_priority); //ensures that the worker thread with the highest priority is selected first when handling requests
        for(int j = 0; j<m; j++)
        {
            services[i].pool[j].id = j; //assigns an id to each worker thread 
            pthread_create(&(services[i].threads[j]), NULL, worker_thread_func, &(services[i].pool[j])); //the code creates a new thread for each worker thread in the pool
        }
    }
    pthread_mutex_init(&(lock_count), NULL);
	// for taking number of request
    int r; 
    cout<<"Enter the number request to be made : ";
    cin>>r;
    cout<<"Enter the transaction type and resource required by each request\n";
    Request *requests = new Request[r];
    start_time = std::chrono::high_resolution_clock::now(); //sets the value of start time to the current time
    for(int i = 0; i<r; i++)
    {
        requests[i].request_id = i; // initializes each request in the requests array with an ID
        cin>>requests[i].type;
        cin>>requests[i].resource_needed; //the amount of resources needed for the request
        std::chrono::high_resolution_clock::time_point request_arrival_time = std::chrono::high_resolution_clock::now();  // set the current exact time as arrival time of request
        std::chrono::microseconds elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(request_arrival_time - start_time); //used to convert elapsed time to microseconds
        requests[i].arrival_time = (double)elapsed_time.count()/1e3; // for converting the time in millisecond upto three decimal point
    }
    
    for(int i = 0; i<r; i++)
    {
    	//cout<<"Searching worker thread for request "<<i<<endl;
        int type = requests[i].type; // type of request 
        WorkerThread *selected_thread = findWorkerThread(type, requests[i]); //find a suitable worker thread for the current request
        if(selected_thread == NULL)
        {
            if(check_possibility(type , requests[i] , services[type].pool)) // if this function returns true then add that request to blocked queue 
            {
                blocked_requests.push(i);
                //cout<<"Blocking request "<<i<<endl;
            }
            else
                rejected_requests.push(i);
        }
        else
        {
            //cout<<"Request "<<i<<" is assigned to thread "<<selected_thread->id<<endl;
            pthread_mutex_lock(&(selected_thread->lock)); // acquires the lock associated with the selected thread
            (selected_thread->request_queue).push(&requests[i]); // address of the current request is then push to the request queue
            pthread_cond_signal(&selected_thread->cond); //function is called to signal that the thread has new work to do
            pthread_mutex_unlock(&(selected_thread->lock)); // release the lock
        }
    }
    int blockedRequests = blocked_requests.size();
    servicable_requests = r - rejected_requests.size(); //request which can be serviced
    
    while(blocked_requests.size() > 0) // run if any request is still blocked
    {
        int b = blocked_requests.front();
        blocked_requests.pop();
        int type = requests[b].type; // finding the type of request
        WorkerThread *selected_thread = findWorkerThread(type, requests[b]); // find a suitable worker thread for the current request
        if(selected_thread == NULL) // if there is still not much resource to complete the request then again push into block state
        {
            blocked_requests.push(b);
        }
        else
        {
            pthread_mutex_lock(&(selected_thread->lock)); // acquire the lock
            (selected_thread->request_queue).push(&requests[b]); // address is pushed to the request queue
            pthread_cond_signal(&selected_thread->cond); // signaling the thread that request is ready to execute
            pthread_mutex_unlock(&(selected_thread->lock)); // release the lock
        }
        
    }

    while(1)
    {
        pthread_mutex_lock(&lock_count);
        
        if(executed_requests == servicable_requests) // if all the request has served then we are going to wake all the threads 
        {
            for(int i = 0; i<n; i++)
            {
                for(int j = 0; j<m; j++)
                {
                    
                    pthread_mutex_lock(&(services[i].pool[j].lock));
                    pthread_cond_signal(&(services[i].pool[j].cond));
                    pthread_mutex_unlock(&(services[i].pool[j].lock));
                }
            }
            pthread_mutex_unlock(&lock_count);
            break;
        }
        pthread_mutex_unlock(&lock_count);
        usleep(100000);
    }
    for(int i = 0; i<n; i++)
    {
        for(int j = 0; j<m; j++)
        {
            pthread_join(services[i].threads[j], NULL); // joining to parent thread after all execution
        }
    }
    system("clear");
    cout<<"\nOUTPUT : \n\nExecution Order : | ";
    for(int i = 0; i<order_of_execution.size(); i++)
    {
        cout<<order_of_execution[i]<<" | "; // this loop is for printing the order in which request got executed 
    }
    cout<<"\n\n";
    // Define the width of each column in the table
    const int REQUEST_NO_WIDTH = 12;
    const int REQUEST_TYPE_WIDTH = 20;
    const int ARRIVAL_TIME_WIDTH = 23;
    const int COMPLETION_TIME_WIDTH = 25;
    const int TURN_AROUND_TIME_WIDTH = 27;
    const int WAITING_TIME_WIDTH = 21;

    // Print the table header
    cout << setfill('-') << setw(REQUEST_NO_WIDTH + REQUEST_TYPE_WIDTH + ARRIVAL_TIME_WIDTH + COMPLETION_TIME_WIDTH + TURN_AROUND_TIME_WIDTH + WAITING_TIME_WIDTH + 19) << "" << endl;
    cout << setfill(' ') << "| " << setw(REQUEST_NO_WIDTH) << left << "Request No." << " | "
        << setw(REQUEST_TYPE_WIDTH) << left << "Request Type" << " | "
        << setw(ARRIVAL_TIME_WIDTH) << left << "Arrival Time(ms)" << " | "
        << setw(COMPLETION_TIME_WIDTH) << left << "Completion Time(ms)" << " | "
        << setw(TURN_AROUND_TIME_WIDTH) << left << "Turn Around Time(ms)" << " | "
        << setw(WAITING_TIME_WIDTH) << left << "Waiting Time(ms)" << " |" << endl;
    cout << setfill('-') << setw(REQUEST_NO_WIDTH + REQUEST_TYPE_WIDTH + ARRIVAL_TIME_WIDTH + COMPLETION_TIME_WIDTH + TURN_AROUND_TIME_WIDTH + WAITING_TIME_WIDTH + 19) << "" << endl;

    // Sort the order of execution
    sort(order_of_execution.begin(), order_of_execution.end());
    double avg_tat = 0, avg_wait = 0;
    // Print each row of the table
    for (int i = 0; i < order_of_execution.size(); i++) {
        int j = order_of_execution[i];
        double turn_around_time = requests[j].completion_time - requests[j].arrival_time;
        double waiting_time = turn_around_time - ((double) BURST_TIME) / 1e3;
        avg_tat += turn_around_time;
        avg_wait += waiting_time;
        cout << setfill(' ') << "| " << setw(REQUEST_NO_WIDTH) << left << j << " | "
            << setw(REQUEST_TYPE_WIDTH) << left << requests[j].type << " | "
            << setw(ARRIVAL_TIME_WIDTH) << left << requests[j].arrival_time << " | "
            << setw(COMPLETION_TIME_WIDTH) << left << requests[j].completion_time << " | "
            << setw(TURN_AROUND_TIME_WIDTH) << left << turn_around_time << " | "
            << setw(WAITING_TIME_WIDTH) << left << waiting_time << " |" << endl;
    }
    cout << setfill('-') << setw(REQUEST_NO_WIDTH + REQUEST_TYPE_WIDTH + ARRIVAL_TIME_WIDTH + COMPLETION_TIME_WIDTH + TURN_AROUND_TIME_WIDTH + WAITING_TIME_WIDTH + 19) << "" << endl;
    avg_tat /= servicable_requests;
    avg_wait /= servicable_requests;
    cout << setfill(' ') << "| " << setw(REQUEST_NO_WIDTH) << left << "           " << " | "
        << setw(REQUEST_TYPE_WIDTH) << left << "            " << " | "
        << setw(ARRIVAL_TIME_WIDTH) << left << "                " << " | "
        << setw(COMPLETION_TIME_WIDTH) << left << "Average" << " | "
        << setw(TURN_AROUND_TIME_WIDTH) << left << avg_tat << " | "
        << setw(WAITING_TIME_WIDTH) << left << avg_wait << " |" << endl;
    cout << setfill('-') << setw(REQUEST_NO_WIDTH + REQUEST_TYPE_WIDTH + ARRIVAL_TIME_WIDTH + COMPLETION_TIME_WIDTH + TURN_AROUND_TIME_WIDTH + WAITING_TIME_WIDTH + 19) << "" << endl;
    
    cout<<"\nBlocked Requests : "<<blockedRequests<<endl;
    cout<<"Rejected Requests : "<<rejected_requests.size()<<endl;
    for(int i = 0; i<n; i++)
    {
        for(int j = 0; j<m; j++)
        {
            pthread_mutex_destroy(&(services[i].pool[j].lock)); // free all the resource used for mutex creation 
        }
        delete[] services[i].threads; 
    }
    pthread_mutex_destroy(&lock_count);// free all resource
    delete[] services;
    return 0; // exit
}
