#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#include "ex_thread.h"
#define BUFF_LEN 50

/*
 * This module generates as many kernel threads as you tell him (the default value is 2) 
 * this is achieved with a double pointer to task_struct that is allocated in the init function.
 * The action of the threads is pretty straightforward, they add a node to a list and write
 * the activation number and kernel id on the buffer of the node.
 * The threads synchronize competitively with the aid of a mutex.
 *
 * There is a Miscdevice that has a read function that prints the buffers of the nodes of the list.
 * The Miscdevice also has a write function that synchronize competitively with the kernel threads
 * to write something on the list.
 */

struct node {
        char buff[BUFF_LEN];
        struct node * next;
};

// Head and Tail of the List
static struct node * head = NULL;
static struct node * tail = NULL;
static int counter = 0;

// Adds a Node at the end of the list 
int list_write(int kthread_id) {

        // Creates New Node
        if (head == NULL && tail == NULL) {
                head = (struct node *) kmalloc(sizeof(struct node), GFP_USER);
                if (head == NULL) return -1;
                head->next = NULL;
                tail = head;
        }
        else {
                tail->next = (struct node *) kmalloc(sizeof(struct node), GFP_USER);
                if (tail->next == NULL) return -1;
                tail = tail->next;
                tail->next = NULL;
        }
        
        // Write on Node
        sprintf(tail->buff, "Activation %d Written by Kthread No %d\n", counter++, kthread_id);
        return 0;
}

// Deletes the First Element of the List
void list_delete_head(void) 
{
        struct node * tmp;

        // Destroys Head
        tmp = head;
        head = head->next;
        kfree(tmp);

}

// Competitive and Cooperative Synch
static struct mutex buff_m;
struct completion available_data;

// Variables used for kthreads
static int kernel_threads_created;
static int number_of_kthreads = 2;
static int mperiod = 2000;           // Milliseconds
static struct task_struct ** out_id;

// Get Input from User
module_param(mperiod, int, 0);
module_param(number_of_kthreads, int, 0);


static int output_thread(void *arg)
{
        int kthread_id, err;
        kthread_id = kernel_threads_created++;

        while (!kthread_should_stop()) {

                mutex_lock(&buff_m);
                err = list_write(kthread_id); // Adds a new node and writes on its buffer
                if (err) {
                    mutex_unlock(&buff_m);
                    return -1;
                }    
                if (counter != 1) complete(&available_data);
                mutex_unlock(&buff_m);

                msleep(mperiod);
        }

        return 0;
}

int thread_create(void)
{
        // Initialize Mutex and Completion
        mutex_init(&buff_m);
        init_completion(&available_data);
  
        // Initialize Vector of Kthreads
        if (number_of_kthreads < 1) number_of_kthreads = 1;
        out_id = kmalloc(sizeof(struct task_struct)*number_of_kthreads, GFP_USER);
        if (out_id == NULL) return -1;

        // Create Kernel Threads
        for (int i = 0; i < number_of_kthreads; i++) {        
                out_id[i] = kthread_run(output_thread, NULL, "out_thread");
                if (IS_ERR(out_id[i])) {
                        printk("Error creating kernel thread!\n");
                        return PTR_ERR(out_id[i]);
                }
        }

        return 0;
}

void thread_destroy(void)
{
        for (int i = 0; i < number_of_kthreads; i++) {        
                if (out_id[i] != NULL) kthread_stop(out_id[i]);
        }
}

int get_data(char *p, int size)
{
        int len, res;

        mutex_lock(&buff_m);
        len = BUFF_LEN;
        if (len > size) len = size; 

          
        // Reads From Buffer
        if (head == NULL) BUG(); // This should not happen
        res = copy_to_user(p,head->buff, len);
        if (res) len = -1;

        list_delete_head();

        // Releases Mutex and Returns Number of Characters Read
        mutex_unlock(&buff_m);
        return len;
}


int write_data(const char * buf, int size)
{
        int err;

        if (size > BUFF_LEN) return -1;
        mutex_lock(&buff_m);

        // Allocates new Node
        tail->next = (struct node *) kmalloc(sizeof(struct node), GFP_USER);
        if (tail->next == NULL) {
                mutex_unlock(&buff_m);
                return 0;
        }
        tail = tail->next;
        tail->next = NULL;
        
        // Writes on Node
        err = copy_from_user(tail->buff, buf, size);
        if (err) {
                mutex_unlock(&buff_m);
                return err;
        }

        mutex_unlock(&buff_m);
        return size;

}
