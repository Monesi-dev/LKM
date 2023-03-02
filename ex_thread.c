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
        struct list_head kl;
};

// Head and Tail of the List
static struct list_head head;
static int counter = 0;

// Adds a Node at the end of the list 
int list_write(int kthread_id) {

        struct node * new_element;
        
        // Creates New Element 
        new_element = kmalloc(sizeof(struct node), GFP_KERNEL);
        if (new_element == NULL) return -1;
        sprintf(new_element->buff, "Activation %d Written by Kthread No %d\n", counter++, kthread_id);

        // Adds New Element to the End of the List
        list_add_tail(&(new_element->kl), &head);

        return 0;
}


// Competitive and Cooperative Synch
static struct mutex buff_m;
struct completion available_data;

// Variables used for kthreads
static int number_of_kthreads_created;
static int number_of_kthreads = 2;
static int mperiod = 2000;           // Milliseconds
static struct task_struct ** out_id;

// Get Input from User
module_param(mperiod, int, 0);
module_param(number_of_kthreads, int, 0);


static int output_thread(void *arg)
{
        int kthread_id, err;
        kthread_id = number_of_kthreads_created++;

        while (!kthread_should_stop()) {

                mutex_lock(&buff_m);
                err = list_write(kthread_id); // Adds a new node and writes on its buffer
                if (err) {
                    mutex_unlock(&buff_m);
                    return -1;
                }    
                mutex_unlock(&buff_m);
                complete(&available_data);

                msleep(mperiod);
        }

        return 0;
}

int thread_create(void)
{
        // Initialize Mutex, Completion and Kernel List
        mutex_init(&buff_m);
        init_completion(&available_data);
        INIT_LIST_HEAD(&head);
  
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
        struct list_head * l, * tmp;
        struct node * elem_read;
        int cnt, res;

        mutex_lock(&buff_m);
        if (size > BUFF_LEN) size = BUFF_LEN; 

        // Reads and Deletes only the first Element of the List
        cnt = 0;
        list_for_each_safe(l, tmp, &head) {

                if (cnt == 0) { 
                        // Reads First Element of the List
                        elem_read = list_entry(l, struct node, kl);
                        res = copy_to_user(p,elem_read->buff, size);
                        if (res) res = -1;

                        // Deletes First Element of the List
                        list_del(l);
                        kfree(elem_read);

                        cnt = 1;
                }

        }

        // Releases Mutex and Returns Number of Characters Read
        mutex_unlock(&buff_m);
        return size;
}


int write_data(const char * buf, int size)
{
        int err;
        struct node * new_element;

        if (size > BUFF_LEN) return -1;
        mutex_lock(&buff_m);

        // Creates New Element 
        new_element = kmalloc(sizeof(struct node), GFP_KERNEL);
        if (new_element == NULL) {
                mutex_unlock(&buff_m);
                return -1;
        }

        // Writes on the Buffer of the Element
        err = copy_from_user(new_element->buff, buf, size);
        if (err) {
                mutex_unlock(&buff_m);
                return err;
        }

        // Adds New Element to the End of the List
        list_add_tail(&(new_element->kl), &head);
        mutex_unlock(&buff_m);

        return size;

}
