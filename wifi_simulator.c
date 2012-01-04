/*
 * This file implements a basic simulator used to verify a simple modification
 * of the 802.11 MAC protocol for Wireless Networks
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Defines */

#define MAX_SLOT_SIZE           100000
#define MAX_PKT_SIZE            100
#define MAX_NODE_COUNT          1000
#define MAX_CW_SIZE             512

#define SLOT_STATE_IDLE         0
#define SLOT_STATE_TRANSMISSION 1
#define SLOT_STATE_COLLISION    2

#define INVALID_BACKOFF        -1

/* Globals */

typedef struct slot_ {
    int    state;
} slot_t;

typedef struct node_ {
    int    backoff;
    int    cw_size;
    int    prev_state;
} node_t;

slot_t slots[MAX_SLOT_SIZE];
node_t nodes[MAX_NODE_COUNT];
int slot_size, pkt_size, node_count, cw_size;
int idle_slots, collision_slots, transmission_slots, packet_count;

/*
 * Main entry point
 */
int 
main (int argc, char *argv[])
{
    int collision_count, colliding_nodes[100]; 
    int status;
    int i, j, k;
    float cur_efficiency = 0.0, prev_efficiency = 0.000001;
    float cur_delta = 1.0, prev_delta = 1.0;

    if (argc != 4) {
        printf("syntax: ./Simulation <pkt-size> <node-count> <cw-size>\n");
        exit(0);
    }

    /* Validate the inputs */
    pkt_size = atoi(argv[1]);
    node_count = atoi(argv[2]);
    cw_size = atoi(argv[3]);

    if ((pkt_size > MAX_PKT_SIZE) || (node_count > MAX_NODE_COUNT) || 
        (cw_size > MAX_CW_SIZE)) {
        printf("Error taking inputs!\n");
        exit(1);
    }

    /* 
     * Slot size can be infinite. For this program, we will assume that it
     * can't exceed one million slots.
     */
    slot_size = MAX_SLOT_SIZE;

    /* Initialize the data structures */
    for (i = 0; i < slot_size; i++) {
        slots[i].state = SLOT_STATE_IDLE;
    }

    for (i = 0; i < node_count; i++) {
        nodes[i].backoff = INVALID_BACKOFF;
        nodes[i].cw_size = cw_size;
        nodes[i].prev_state = SLOT_STATE_IDLE;
    }

    /* Initialize the random seed generator */
    srand(time(NULL));

    /* 
     * Main loop. For each slot, do the following:
     *
     * 1. For each node, check if the slot is free. If it is, decrement
     *    backoff. If backoff expires, transmit the packet.
     * 2. If the slot is not free, wait till it becomes free and then
     *    decrement backoff
     * 3. If 2 nodes simultaneoulsly expire their backoff, then there will
     *    be a collision for packet-size slots.
     * 4. In case of a collision, the colliding nodes double their CW size.
     */

    for (i = 0; i < slot_size; i++) {

        /* Reset the collision count */
        collision_count = 0;

        /* For each node */
        for (j = 0; j < node_count; j++) {

            if (slots[i].state == SLOT_STATE_IDLE &&
                nodes[j].prev_state == SLOT_STATE_IDLE) {
                /*
                 * Both the current and previous slots are idle. Decrement
                 * backoff. Initialize backoff if we are coming here for the
                 * first time.
                 */
                if (nodes[j].backoff == INVALID_BACKOFF) { 
                    nodes[j].backoff = (rand() % nodes[j].cw_size) + 1;
                }

                nodes[j].backoff -= 1;
                if (nodes[j].backoff == 0) {
                    /* 
                     * Ready to transmit the packet. Make a note of all 
                     * such nodes whose backoff expires in the same slot. 
                     * This will help us determine if there was a 
                     * successful transmission in this slot.
                     */
                    colliding_nodes[collision_count++] = j;
                }
            } else if (slots[i].state == SLOT_STATE_IDLE &&
                       nodes[j].prev_state != SLOT_STATE_IDLE) {
                /*
                 * We need to sense an idle slot for the full slot duration.
                 * Hence this check. Once the previous state and the current
                 * state are both idle, we decrement backoff.
                 */
                nodes[j].prev_state = SLOT_STATE_IDLE;
            } else {
                /*
                 * When we are decrementing our backoff, some other node's
                 * backoff may expire and it may start transmitting. In such
                 * a case,we should freeze our backoff counter till the other
                 * transmission is complete. To acheive this, we set the
                 * node's slot state accordingly.
                 */
                nodes[j].prev_state = slots[i].state;
            }
        }

        /*
         * We have processed all the nodes. Check the collision count. There 
         * can be 3 cases:
         *
         * 1. collision_count = 0: None of the nodes expired their backoff.
         *    The slot state will either be transmission, collision or idle.
         * 2. collision_count = 1: Only one guy expired the backoff. This
         *    indicates successful transmission.
         * 3. collision_count > 1: Since many guys transmit simultaneously,
         *    there will be a collision
         */
        switch (collision_count) {
            case 0:
                /* 
                 * No change in slot state. It will already be set to 
                 * transmission, collision or idle. Nothing to update here.
                 */
                break;

            case 1:
                /* Successful transmission */
                for (k = i; k < (i + pkt_size); k++) {
                    slots[k].state = SLOT_STATE_TRANSMISSION;
                }

                /* Reset the backoff counter */
                nodes[colliding_nodes[0]].backoff = INVALID_BACKOFF;

                /* Update the packet count */
                packet_count++;

                break;

            default:
                /*
                 * Collision. First set the slot state for the affected slots.
                 * For each colliding node, double the CW size and reset the 
                 * backoff counter.
                 */
                for (k = i; k < (i + pkt_size); k++) {
                    slots[k].state = SLOT_STATE_COLLISION;
                }

                for (k = 0; k < collision_count; k++) {
                    nodes[colliding_nodes[k]].backoff = INVALID_BACKOFF;
                    nodes[colliding_nodes[k]].cw_size *= 2;
                }

                break;
        }

        /* Collect Statistics */
        if (slots[i].state == SLOT_STATE_IDLE) {
            idle_slots++;
        } else if (slots[i].state == SLOT_STATE_TRANSMISSION) {
            transmission_slots++;
        } else {
            collision_slots++;
        }

        /*
         * We use the following criteria to determine when to stop the
         * simulation. At each slot boundary, we calculate the efficiency
         * and store the delta i.e efficiency calculated at this slot minus
         * the efficiency calculated at the previous slot. We compare this
         * delta with the delta obtained in the previous iteration. If both
         * the deltas are less than 0.05%, then we conclude that the 
         * simulation has converged.
         */
        if (((i % 1000) == 0) && (i != 0)) {
            cur_efficiency = (float)transmission_slots / (float)i;
            cur_delta = (cur_efficiency > prev_efficiency) ?
                        (cur_efficiency - prev_efficiency) :
                        (prev_efficiency - cur_efficiency);

            if (cur_delta < 0.0005 && prev_delta < 0.0005) {
                break;
            }

            prev_efficiency = cur_efficiency;
            prev_delta = cur_delta;
        }
    }

    if (i >= slot_size) {
        /*
         * For some reason, our simulation didn't converge. Complain and
         * bail.
         */
        printf("Simulation failed to converge. Exiting...\n");
        exit(1);
    }

    printf("Idle Slots: %d\n", idle_slots);
    printf("Transmission Slots: %d\n", transmission_slots);
    printf("Collision Slots: %d\n", collision_slots);
    printf("Packets successfully transmitted: %d\n", packet_count);
    printf("Total slots used for simulation: %d\n", i);
    
    printf("Throughput: %f\n", (float)packet_count / (float)i);
    printf(" %d %f\n", cw_size, (float)transmission_slots / (float)i);

    return 0;
}

/* End of File */
