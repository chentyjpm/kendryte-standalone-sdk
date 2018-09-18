/* Copyright 2018 Canaan Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <stddef.h>
#include <stdint.h>
#include "encoding.h"
#include "clint.h"
#include "sysctl.h"

struct clint_timer_instance_t
{
    uint64_t interval;
    uint64_t cycles;
    uint64_t single_shot;
    clint_timer_callback_t callback;
    void* ctx;
};

struct clint_ipi_instance_t
{
    clint_ipi_callback_t callback;
    void* ctx;
};

volatile struct clint_t* const clint = (volatile struct clint_t*)CLINT_BASE_ADDR;
static struct clint_timer_instance_t clint_timer_instance[CLINT_NUM_HARTS];
static struct clint_ipi_instance_t clint_ipi_instance[CLINT_NUM_HARTS];

uint64_t clint_get_time(void)
{
    /* No difference on harts */
    return clint->mtime;
}

int clint_timer_init(void)
{
    /* Read hart id */
    unsigned long hart_id = read_hartid();
    /* Clear the Machine-Timer bit in MIE */
    clear_csr(mie, MIP_MTIP);
    /* Fill hart's instance with original data */

    /* clang-format off */
    clint_timer_instance[hart_id] = (const struct clint_timer_instance_t)
    {
        .interval    = 0,
        .cycles      = 0,
        .single_shot = 0,
        .callback    = NULL,
        .ctx         = NULL,
    };
    /* clang-format on */

    return 0;
}

int clint_timer_stop(void)
{
    /* Clear the Machine-Timer bit in MIE */
    clear_csr(mie, MIP_MTIP);
    return 0;
}

uint64_t clint_timer_get_freq(void)
{
    /* The clock is divided by CLINT_CLOCK_DIV */
    return sysctl_get_freq() / CLINT_CLOCK_DIV;
}

int clint_timer_start(uint64_t interval, int single_shot)
{
    /* Read hart id */
    unsigned long hart_id = read_hartid();
    /* Set timer interval */
    if (clint_timer_set_interval(interval) != 0)
        return -1;
    /* Set timer single shot */
    if (clint_timer_set_single_shot(single_shot) != 0)
        return -1;
    /* Check settings to prevent interval is 0 */
    if (clint_timer_instance[hart_id].interval == 0)
        return -1;
    /* Check settings to prevent cycles is 0 */
    if (clint_timer_instance[hart_id].cycles == 0)
        return -1;
    /* Add cycle interval to mtimecmp */
    uint64_t now = clint->mtime;
    uint64_t then = now + clint_timer_instance[hart_id].cycles;
    /* Set mtimecmp by hart id */
    clint->mtimecmp[hart_id] = then;
    /* Enable interrupts in general */
    set_csr(mstatus, MSTATUS_MIE);
    /* Enable the Machine-Timer bit in MIE */
    set_csr(mie, MIP_MTIP);
    return 0;
}

uint64_t clint_timer_get_interval(void)
{
    /* Read hart id */
    unsigned long hart_id = read_hartid();
    return clint_timer_instance[hart_id].interval;
}

int clint_timer_set_interval(uint64_t interval)
{
    /* Read hart id */
    unsigned long hart_id = read_hartid();
    /* Check parameter */
    if (interval == 0)
        return -1;

    /* Assign user interval with Millisecond(ms) */
    clint_timer_instance[hart_id].interval = interval;
    /* Convert interval to cycles */
    clint_timer_instance[hart_id].cycles = interval * clint_timer_get_freq() / 1000ULL;
    return 0;
}

int clint_timer_get_single_shot(void)
{
    /* Read hart id */
    unsigned long hart_id = read_hartid();
    /* Get single shot mode by hart id */
    return clint_timer_instance[hart_id].single_shot;
}

int clint_timer_set_single_shot(int single_shot)
{
    /* Read hart id */
    unsigned long hart_id = read_hartid();
    /* Set single shot mode by hart id */
    clint_timer_instance[hart_id].single_shot = single_shot;
    return 0;
}

int clint_timer_register(clint_timer_callback_t callback, void* ctx)
{
    /* Read hart id */
    unsigned long hart_id = read_hartid();
    /* Set user callback function */
    clint_timer_instance[hart_id].callback = callback;
    /* Assign user context */
    clint_timer_instance[hart_id].ctx = ctx;
    return 0;
}

int clint_timer_deregister(void)
{
    /* Just assign NULL to user callback function and context */
    return clint_timer_register(NULL, NULL);
}

int clint_ipi_init(void)
{
    /* Read hart id */
    unsigned long hart_id = read_hartid();
    /* Clear the Machine-Software bit in MIE */
    clear_csr(mie, MIP_MSIP);
    /* Fill hart's instance with original data */
    /* clang-format off */
    clint_ipi_instance[hart_id] = (const struct clint_ipi_instance_t){
        .callback    = NULL,
        .ctx         = NULL,
    };
    /* clang-format on */

    return 0;
}

int clint_ipi_enable(void)
{
    /* Enable interrupts in general */
    set_csr(mstatus, MSTATUS_MIE);
    /* Set the Machine-Software bit in MIE */
    set_csr(mie, MIP_MSIP);
    return 0;
}

int clint_ipi_disable(void)
{
    /* Clear the Machine-Software bit in MIE */
    clear_csr(mie, MIP_MSIP);
    return 0;
}

int clint_ipi_send(size_t hart_id)
{
    if (hart_id >= CLINT_NUM_HARTS)
        return -1;
    clint->msip[hart_id].msip = 1;
    return 0;
}

int clint_ipi_clear(size_t hart_id)
{
    if (hart_id >= CLINT_NUM_HARTS)
        return -1;
    if (clint->msip[hart_id].msip)
    {
        clint->msip[hart_id].msip = 0;
        return 1;
    }
    return 0;
}

int clint_ipi_register(clint_ipi_callback_t callback, void* ctx)
{
    /* Read hart id */
    unsigned long hart_id = read_hartid();
    /* Set user callback function */
    clint_ipi_instance[hart_id].callback = callback;
    /* Assign user context */
    clint_ipi_instance[hart_id].ctx = ctx;
    return 0;
}

int clint_ipi_deregister(void)
{
    /* Just assign NULL to user callback function and context */
    return clint_ipi_register(NULL, NULL);
}

uintptr_t handle_irq_m_timer(uintptr_t cause, uintptr_t epc, uintptr_t regs[32])
{
    /* Read hart id */
    uint64_t hart_id = read_hartid();
    uint64_t ie_flag = read_csr(mie);

    clear_csr(mie, MIP_MTIP | MIP_MSIP);
    set_csr(mstatus, MSTATUS_MIE);
    if (clint_timer_instance[hart_id].callback != NULL)
        clint_timer_instance[hart_id].callback(
            clint_timer_instance[hart_id].ctx);
    clear_csr(mstatus, MSTATUS_MIE);
    set_csr(mstatus, MSTATUS_MPIE | MSTATUS_MPP);
    write_csr(mie, ie_flag);
    /* If not single shot and cycle interval is not 0, repeat this timer */
    if (!clint_timer_instance[hart_id].single_shot && clint_timer_instance[hart_id].cycles != 0)
    {
        /* Set mtimecmp by hart id */
        clint->mtimecmp[hart_id] += clint_timer_instance[hart_id].cycles;
    }
    else
        clear_csr(mie, MIP_MTIP);
    return epc;
}

uintptr_t handle_irq_m_soft(uintptr_t cause, uintptr_t epc, uintptr_t regs[32])
{
    /* Read hart id */
    uint64_t hart_id = read_hartid();
    /* Clear the Machine-Software bit in MIE to prevent call again */
    clear_csr(mie, MIP_MSIP);
    set_csr(mstatus, MSTATUS_MIE);
    /* Clear ipi flag */
    clint_ipi_clear(hart_id);
    if (clint_ipi_instance[hart_id].callback != NULL)
        clint_ipi_instance[hart_id].callback(clint_ipi_instance[hart_id].ctx);
    clear_csr(mstatus, MSTATUS_MIE);
    set_csr(mstatus, MSTATUS_MPIE | MSTATUS_MPP);
    set_csr(mie, MIP_MSIP);
    return epc;
}
