/**
 * MA-Memphis
 * @file task_migration.c
 *
 * @author Angelo Elias Dalzotto (angelo.dalzotto@edu.pucrs.br)
 * GAPH - Hardware Design Support Group (https://corfu.pucrs.br/)
 * PUCRS - Pontifical Catholic University of Rio Grande do Sul (http://pucrs.br/)
 * 
 * @date March 2021
 * 
 * @brief Task migration functions
 */

#include <stddef.h>

#include <memphis.h>
#include <stdlib.h>
#include <stdio.h>

#include "task_migration.h"
#include "window.h"
#include "sliding_window.h"
#include "services.h"
#include "app.h"

void tm_migrate(mapper_t *mapper, int task_id)
{
	printf("Received migration request to task id %d at time %d\n", task_id, memphis_get_tick());

	app_t *app = app_search(mapper->apps, task_id >> 8);
	if(app == NULL){
		printf("Not found app to migrate with id %d\n", task_id >> 8);
		return;
	}

	task_t *task = app->task[task_id & 0xFF];
	if(task->id != task_id){
		printf("Not found task to migrate with id %d\n", task_id);
		return;
	}
	if(task->status != RUNNING){
		puts("Will not migrate. Task is either already migrating or waiting for task release\n");
		return;
	}

	task->old_proc = task->proc_idx;
	// unsigned then = GetTick();

	/* Check the window center disregarding the task to migrate */
	app->center_x = 0;
	app->center_y = 0;
	for(int i = 0; i < app->task_cnt; i++){
		if(i == (task_id & 0xFF))
			continue;

		app->center_x += mapper->processors[app->task[i]->proc_idx].addr >> 8;
		app->center_y += mapper->processors[app->task[i]->proc_idx].addr & 0xFF;
	}
	app->center_x /= (app->task_cnt - 1);
	app->center_y /= (app->task_cnt - 1);

	/* Get window from this center (able to grow) */
	window_t window;
	window_set_from_center(&window, mapper->processors, app, 1, MAP_MIN_WX, MAP_MIN_WY, false);

	/* Map to the specific window */
	task->proc_idx = sw_map_task(task, app, mapper->processors, &window);

	// unsigned now = GetTick();
	// Echo("Ticks of mapping task for migration = "); Echo(itoa(now - then)); Echo("\n");

	if(task->old_proc == task->proc_idx){
		puts("Will not migrate. Task is in the same PE as the target address\n");
		return;
	}

	printf("Migrating task to address %d\n", mapper->processors[task->proc_idx].addr);

	/* Allocate the page on target address */
	mapper->processors[task->proc_idx].free_page_cnt--;
	mapper->available_slots--;

	/* Mark the task as migrating */
	task->status = MIGRATING;

	/* Send migration order to Kernel at old processor address */
	message_t msg;
	msg.payload[0] = TASK_MIGRATION;
	msg.payload[1] = task->id;
	msg.payload[2] = mapper->processors[task->proc_idx].addr;
	msg.length = 3;
	memphis_send_any(&msg, MEMPHIS_KERNEL_MSG | mapper->processors[task->old_proc].addr);
}

void tm_migration_complete(mapper_t *mapper, int task_id)
{
	printf("Received migration completed to task id %d at time %d\n", task_id, memphis_get_tick());

	app_t *app = app_search(mapper->apps, task_id >> 8);
	if(app == NULL){
		printf("Not found app that migrated with id %d\n", task_id >> 8);
		return;
	}

	task_t *task = app->task[task_id & 0xFF];
	if(task->id != task_id){
		printf("Not found task that migrated with id %d\n", task_id);
		return;
	}

	if(task->status != MIGRATING){
		puts("Will not free old resources. Task not marked as migrating. Already freed?\n");
		return;
	}

	/* Mark as migrationg finished */
	task->status = RUNNING;

	/* Free old processor resources */
	mapper->processors[task->old_proc].free_page_cnt++;
	mapper->available_slots++;
}
