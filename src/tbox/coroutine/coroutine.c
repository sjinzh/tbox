/*!The Treasure Box Library
 * 
 * TBox is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 * 
 * TBox is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with TBox; 
 * If not, see <a href="http://www.gnu.org/licenses/"> http://www.gnu.org/licenses/</a>
 * 
 * Copyright (C) 2009 - 2017, ruki All rights reserved.
 *
 * @author      ruki
 * @file        coroutine.h
 * @ingroup     coroutine
 *
 */

/* //////////////////////////////////////////////////////////////////////////////////////
 * includes
 */
#include "coroutine.h"
#include "impl/impl.h"

/* //////////////////////////////////////////////////////////////////////////////////////
 * private implementation
 */
tb_coroutine_ref_t tb_coroutine_init(tb_scheduler_ref_t scheduler, tb_coroutine_func_t func, tb_cpointer_t priv, tb_size_t stacksize)
{
    tb_trace_noimpl();
    return tb_null;
}
tb_void_t tb_coroutine_exit(tb_coroutine_ref_t coroutine)
{
    tb_trace_noimpl();
}
tb_void_t tb_coroutine_switch(tb_coroutine_ref_t coroutine)
{
    tb_trace_noimpl();
}
tb_size_t tb_coroutine_state(tb_coroutine_ref_t coroutine)
{
    tb_trace_noimpl();
    return 0;
}
tb_size_t tb_coroutine_state_set(tb_coroutine_ref_t coroutine)
{
    tb_trace_noimpl();
    return 0;
}

/* //////////////////////////////////////////////////////////////////////////////////////
 * implementation
 */
tb_bool_t tb_coroutine_start(tb_scheduler_ref_t scheduler, tb_coroutine_func_t func, tb_cpointer_t priv, tb_size_t stacksize)
{
    tb_trace_noimpl();
    return tb_false;
}
tb_void_t tb_coroutine_yield()
{
    tb_trace_noimpl();
}
tb_void_t tb_coroutine_sleep(tb_long_t interval)
{
    tb_trace_noimpl();
}
tb_coroutine_ref_t tb_coroutine_self()
{
    tb_trace_noimpl();
    return tb_null;
}
