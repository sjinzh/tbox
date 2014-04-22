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
 * Copyright (C) 2009 - 2015, ruki All rights reserved.
 *
 * @author		ruki
 * @file		async_stream.c
 * @ingroup 	stream
 *
 */

/* //////////////////////////////////////////////////////////////////////////////////////
 * trace
 */
#define TB_TRACE_MODULE_NAME 				"astream"
#define TB_TRACE_MODULE_DEBUG 				(1)

/* //////////////////////////////////////////////////////////////////////////////////////
 * includes
 */
#include "stream.h"
#include "../network/network.h"
#include "../platform/platform.h"

/* //////////////////////////////////////////////////////////////////////////////////////
 * implementation
 */
static tb_bool_t tb_async_stream_csync_func(tb_async_stream_t* astream, tb_size_t state, tb_byte_t const* data, tb_size_t real, tb_size_t size, tb_pointer_t priv)
{
	// check
	tb_assert_and_check_return_val(astream && astream->sync && astream->wcache_and.sync.func, tb_false);

	// move cache
	if (real) tb_scoped_buffer_memmov(&astream->wcache_data, real);

	// not finished? continue it
	if (state == TB_STATE_OK && real < size) return tb_true;

	// not finished? continue it
	tb_bool_t ok = tb_false;
	if (state == TB_STATE_OK && real < size) ok = tb_true;
	// ok? sync it
	else if (state == TB_STATE_OK && real == size)
	{
		// check
		tb_assert_and_check_return_val(!tb_scoped_buffer_size(&astream->wcache_data), tb_false);

		// post sync
		ok = astream->sync(astream, astream->wcache_and.sync.bclosing, astream->wcache_and.sync.func, priv);

		// failed? done func
		if (!ok) ok = astream->wcache_and.sync.func(astream, TB_STATE_UNKNOWN_ERROR, astream->wcache_and.sync.bclosing, priv);
	}
	// failed?
	else
	{
		// failed? done func
		ok = astream->wcache_and.sync.func(astream, state != TB_STATE_OK? state : TB_STATE_UNKNOWN_ERROR, astream->wcache_and.sync.bclosing, priv);
	}

	// ok?
	return ok;
}
static tb_bool_t tb_async_stream_cwrit_func(tb_async_stream_t* astream, tb_size_t state, tb_byte_t const* data, tb_size_t real, tb_size_t size, tb_pointer_t priv)
{
	// check
	tb_assert_and_check_return_val(astream && astream->wcache_and.writ.func, tb_false);

	// done
	tb_bool_t bwrit = tb_true;
	do
	{
		// check
		tb_check_break(state == TB_STATE_OK);

		// finished?
		if (real == size)
		{
			// trace
			tb_trace_d("cache: writ: %lu: ok", astream->wcache_and.writ.size);

			// clear cache
			tb_scoped_buffer_clear(&astream->wcache_data);
	
			// done func
			astream->wcache_and.writ.func(astream, TB_STATE_OK, astream->wcache_and.writ.data, astream->wcache_and.writ.size, astream->wcache_and.writ.size, priv);

			// break
			bwrit = tb_false;
		}

	} while (0);

	// failed? 
	if (state != TB_STATE_OK)
	{
		// trace
		tb_trace_d("cache: writ: %lu: failed: %s", astream->wcache_and.writ.size, tb_state_cstr(state));

		// done func
		astream->wcache_and.writ.func(astream, state, astream->wcache_and.writ.data, 0, astream->wcache_and.writ.size, priv);

		// break
		bwrit = tb_false;
	}

	// continue writing?
	return bwrit;
}
static tb_bool_t tb_async_stream_cwrit_done(tb_async_stream_t* astream, tb_size_t delay, tb_byte_t const* data, tb_size_t size, tb_async_stream_writ_func_t func, tb_pointer_t priv)
{
	// check
	tb_assert_and_check_return_val(astream && astream->writ && data && size && func, tb_false);
	
	// using cache?
	tb_bool_t ok = tb_false;
	if (astream->wcache_maxn)
	{
		// writ data to cache 
		if (data && size) tb_scoped_buffer_memncat(&astream->wcache_data, data, size);

		// the writ data and size
		tb_byte_t const* 	writ_data = tb_scoped_buffer_data(&astream->wcache_data);
		tb_size_t 			writ_size = tb_scoped_buffer_size(&astream->wcache_data);
	
		// no full? writ ok
		if (writ_size < astream->wcache_maxn)
		{
			// trace
			tb_trace_d("cache: writ: %lu: ok", size);

			// done func
			func(astream, TB_STATE_OK, data, size, size, priv);
			ok = tb_true;
		}
		else
		{
			// trace
			tb_trace_d("cache: writ: %lu: ..", size);

			// writ it
			astream->wcache_and.writ.func = func;
			astream->wcache_and.writ.data = data;
			astream->wcache_and.writ.size = size;
			ok = astream->writ(astream, delay, writ_data, writ_size, tb_async_stream_cwrit_func, priv);
		}
	}
	// writ it
	else ok = astream->writ(astream, delay, data, size, func, priv);

	// ok?
	return ok;
}
static tb_bool_t tb_async_stream_cread_done(tb_async_stream_t* astream, tb_size_t delay, tb_size_t size, tb_async_stream_read_func_t func, tb_pointer_t priv)
{
	// check
	tb_assert_and_check_return_val(astream && astream->read && func, tb_false);

	// have writed cache? need sync it first
	tb_assert_and_check_return_val(!astream->wcache_maxn || !tb_scoped_buffer_size(&astream->wcache_data), tb_false);

	// using cache?
	tb_byte_t* data = tb_null;
	if (astream->rcache_maxn)
	{
		// grow data
		if (astream->rcache_maxn > tb_scoped_buffer_maxn(&astream->rcache_data)) 
			tb_scoped_buffer_resize(&astream->rcache_data, astream->rcache_maxn);

		// the cache data
		data = tb_scoped_buffer_data(&astream->rcache_data);
		tb_assert_and_check_return_val(data, tb_false);

		// the maxn
		tb_size_t maxn = tb_scoped_buffer_maxn(&astream->rcache_data);

		// adjust the size
		if (!size || size > maxn) size = maxn;
	}

	// read it
	return astream->read(astream, delay, data, size, func, priv);
}
static tb_bool_t tb_async_stream_oread_func(tb_async_stream_t* astream, tb_size_t state, tb_pointer_t priv)
{
	// check
	tb_async_stream_oread_t* oread = (tb_async_stream_oread_t*)priv;
	tb_assert_and_check_return_val(astream && astream->read && oread && oread->func, tb_false);

	// done
	tb_bool_t ok = tb_true;
	do
	{
		// ok? 
		tb_check_break(state == TB_STATE_OK);

		// reset state
		state = TB_STATE_UNKNOWN_ERROR;
		
		// stoped?
		if (tb_atomic_get(&astream->base.bstoped))
		{
			state = TB_STATE_KILLED;
			break;
		}
	
		// read it
		if (!tb_async_stream_cread_done(astream, 0, oread->size, oread->func, oread->priv)) break;

		// ok
		state = TB_STATE_OK;

	} while (0);
 
	// failed?
	if (state != TB_STATE_OK) 
	{
		// stoped
		tb_atomic_set(&astream->base.bstoped, 1);
 
		// done func
		ok = oread->func(astream, state, tb_null, 0, oread->size, oread->priv);
	}
 
	// ok?
	return ok;
}
static tb_bool_t tb_async_stream_owrit_func(tb_async_stream_t* astream, tb_size_t state, tb_pointer_t priv)
{
	// check
	tb_async_stream_owrit_t* owrit = (tb_async_stream_owrit_t*)priv;
	tb_assert_and_check_return_val(astream && astream->writ && owrit && owrit->func, tb_false);

	// done
	tb_bool_t ok = tb_true;
	do
	{
		// ok? 
		tb_check_break(state == TB_STATE_OK);

		// reset state
		state = TB_STATE_UNKNOWN_ERROR;
			
		// stoped?
		if (tb_atomic_get(&astream->base.bstoped))
		{
			state = TB_STATE_KILLED;
			break;
		}

		// check
		tb_assert_and_check_break(owrit->data && owrit->size);

		// writ it
		if (!tb_async_stream_cwrit_done(astream, 0, owrit->data, owrit->size, owrit->func, owrit->priv)) break;

		// ok
		state = TB_STATE_OK;

	} while (0);

	// failed? 
	if (state != TB_STATE_OK)
	{	
		// stoped
		tb_atomic_set(&astream->base.bstoped, 1);

		// done func
		ok = owrit->func(astream, state, owrit->data, 0, owrit->size, owrit->priv);
	}

	// ok?
	return ok;
}
static tb_bool_t tb_async_stream_oseek_func(tb_async_stream_t* astream, tb_size_t state, tb_pointer_t priv)
{
	// check
	tb_async_stream_oseek_t* oseek = (tb_async_stream_oseek_t*)priv;
	tb_assert_and_check_return_val(astream && astream->seek && oseek && oseek->func, tb_false);

	// done
	tb_bool_t ok = tb_true;
	do
	{
		// ok? 
		tb_check_break(state == TB_STATE_OK);

		// reset state
		state = TB_STATE_UNKNOWN_ERROR;
		
		// stoped?
		if (tb_atomic_get(&astream->base.bstoped))
		{
			state = TB_STATE_KILLED;
			break;
		}

		// offset be not modified?
		if (tb_stream_offset(astream) == oseek->offset)
		{
			// done func
			ok = oseek->func(astream, TB_STATE_OK, oseek->offset, oseek->priv);
		}
		else
		{
			// seek it
			if (!astream->seek(astream, oseek->offset, oseek->func, oseek->priv)) break;
		}

		// ok
		state = TB_STATE_OK;

	} while (0);

	// failed? 
	if (state != TB_STATE_OK) 
	{	
		// stoped
		tb_atomic_set(&astream->base.bstoped, 1);

		// done func
		ok = oseek->func(astream, state, 0, oseek->priv);
	}

	// ok?
	return ok;
}
static tb_bool_t tb_async_stream_sread_func(tb_async_stream_t* astream, tb_size_t state, tb_bool_t bclosing, tb_pointer_t priv)
{
	// check
	tb_async_stream_sread_t* sread = (tb_async_stream_sread_t*)priv;
	tb_assert_and_check_return_val(astream && astream->read && sread && sread->func, tb_false);

	// done
	tb_bool_t ok = tb_true;
	do
	{
		// ok? 
		tb_check_break(state == TB_STATE_OK);

		// reset state
		state = TB_STATE_UNKNOWN_ERROR;
		
		// stoped?
		if (tb_atomic_get(&astream->base.bstoped))
		{
			state = TB_STATE_KILLED;
			break;
		}
	
		// read it
		if (!tb_async_stream_cread_done(astream, 0, sread->size, sread->func, sread->priv)) break;

		// ok
		state = TB_STATE_OK;

	} while (0);
 
	// failed?
	if (state != TB_STATE_OK) 
	{
		// done func
		ok = sread->func(astream, state, tb_null, 0, sread->size, sread->priv);
	}
 
	// ok?
	return ok;
}
static tb_bool_t tb_async_stream_sseek_func(tb_async_stream_t* astream, tb_size_t state, tb_bool_t bclosing, tb_pointer_t priv)
{
	// check
	tb_async_stream_sseek_t* sseek = (tb_async_stream_sseek_t*)priv;
	tb_assert_and_check_return_val(astream && astream->seek && sseek && sseek->func, tb_false);

	// done
	tb_bool_t ok = tb_true;
	do
	{
		// ok? 
		tb_check_break(state == TB_STATE_OK);

		// reset state
		state = TB_STATE_UNKNOWN_ERROR;
		
		// stoped?
		if (tb_atomic_get(&astream->base.bstoped))
		{
			state = TB_STATE_KILLED;
			break;
		}

		// offset be not modified?
		if (tb_stream_offset(astream) == sseek->offset)
		{
			// done func
			ok = sseek->func(astream, TB_STATE_OK, sseek->offset, sseek->priv);
		}
		else
		{
			// seek it
			if (!astream->seek(astream, sseek->offset, sseek->func, sseek->priv)) break;
		}

		// ok
		state = TB_STATE_OK;

	} while (0);

	// failed? 
	if (state != TB_STATE_OK) 
	{	
		// done func
		ok = sseek->func(astream, state, 0, sseek->priv);
	}

	// ok?
	return ok;
}
/* //////////////////////////////////////////////////////////////////////////////////////
 * interfaces
 */
tb_async_stream_t* tb_async_stream_init_from_url(tb_aicp_t* aicp, tb_char_t const* url)
{
	// check
	tb_assert_and_check_return_val(aicp && url, tb_null);

	// the init
	static tb_async_stream_t* (*g_init[])() = 
	{
		tb_null
	,	tb_async_stream_init_file
	,	tb_async_stream_init_sock
	,	tb_async_stream_init_http
	,	tb_async_stream_init_data
	};

	// init
	tb_char_t const* 	p = url;
	tb_async_stream_t* 		astream = tb_null;
	tb_size_t 			type = TB_STREAM_TYPE_NONE;
	if (!tb_strnicmp(p, "http://", 7)) 			type = TB_STREAM_TYPE_HTTP;
	else if (!tb_strnicmp(p, "sock://", 7)) 	type = TB_STREAM_TYPE_SOCK;
	else if (!tb_strnicmp(p, "file://", 7)) 	type = TB_STREAM_TYPE_FILE;
	else if (!tb_strnicmp(p, "data://", 7)) 	type = TB_STREAM_TYPE_DATA;
	else if (!tb_strnicmp(p, "https://", 8)) 	type = TB_STREAM_TYPE_HTTP;
	else if (!tb_strnicmp(p, "socks://", 8)) 	type = TB_STREAM_TYPE_SOCK;
	else if (!tb_strstr(p, "://")) 				type = TB_STREAM_TYPE_FILE;
	else 
	{
		tb_trace_d("[astream]: unknown prefix for url: %s", url);
		return tb_null;
	}
	tb_assert_and_check_goto(type && type < tb_arrayn(g_init) && g_init[type], fail);

	// init stream
	astream = g_init[type](aicp);
	tb_assert_and_check_goto(astream, fail);

	// set url
	if (!tb_stream_ctrl(astream, TB_STREAM_CTRL_SET_URL, url)) goto fail;

	// ok
	return astream;

fail:
	
	// exit stream
	if (astream) tb_async_stream_exit(astream, tb_false);
	return tb_null;
}
tb_void_t tb_async_stream_clos(tb_async_stream_t* astream, tb_bool_t bcalling)
{
	// check
	tb_assert_and_check_return(astream);

	// trace
	tb_trace_d("clos: ..");

	// kill it first 
	tb_stream_kill(astream);

	// clos it
	if (astream->clos) astream->clos(astream, bcalling);

	// not opened
	tb_atomic_set0(&astream->base.bopened);

	// clear wcache
	tb_scoped_buffer_clear(&astream->wcache_data);

	// clear debug info
#ifdef __tb_debug__
	astream->file = tb_null;
	astream->func = tb_null;
	astream->line = 0;
#endif

	// trace
	tb_trace_d("clos: ok");
}
tb_void_t tb_async_stream_exit(tb_async_stream_t* astream, tb_bool_t bcalling)
{
	// check
	tb_assert_and_check_return(astream);

	// trace
	tb_trace_d("exit: ..");

	// close it first
	tb_async_stream_clos(astream, bcalling);

	// exit it
	if (astream->exit) astream->exit(astream, bcalling);

	// exit url
	tb_url_exit(&astream->base.url);

	// exit rcache
	tb_scoped_buffer_exit(&astream->rcache_data);

	// exit wcache
	tb_scoped_buffer_exit(&astream->wcache_data);

	// free it
	tb_free(astream);

	// trace
	tb_trace_d("exit: ok");

}
tb_bool_t tb_async_stream_open_try(tb_async_stream_t* astream)
{
	// check
	tb_assert_and_check_return_val(astream && astream->open, tb_false);
		
	// check state
	tb_assert_and_check_return_val(!tb_atomic_get(&astream->base.bopened), tb_true);
	tb_assert_and_check_return_val(tb_atomic_get(&astream->base.bstoped), tb_false);

	// init state
	tb_atomic_set0(&astream->base.bstoped);

	// try to open it
	tb_bool_t ok = astream->open(astream, tb_null, tb_null);

	// open failed?
	if (!ok) tb_atomic_set(&astream->base.bstoped, 1);

	// ok?
	return ok;
}
tb_bool_t tb_async_stream_open_(tb_async_stream_t* astream, tb_async_stream_open_func_t func, tb_pointer_t priv __tb_debug_decl__)
{
	// check
	tb_assert_and_check_return_val(astream && astream->open && func, tb_false);
	
	// check state
	tb_assert_and_check_return_val(!tb_atomic_get(&astream->base.bopened), tb_false);
	tb_assert_and_check_return_val(tb_atomic_get(&astream->base.bstoped), tb_false);

	// save debug info
#ifdef __tb_debug__
	astream->func = func_;
	astream->file = file_;
	astream->line = line_;
#endif

	// init state
	tb_atomic_set0(&astream->base.bstoped);

	// open it
	tb_bool_t ok = astream->open(astream, func, priv);

	// post failed?
	if (!ok) tb_atomic_set(&astream->base.bstoped, 1);

	// ok?
	return ok;
}
tb_bool_t tb_async_stream_read_(tb_async_stream_t* astream, tb_size_t size, tb_async_stream_read_func_t func, tb_pointer_t priv __tb_debug_decl__)
{
	// read it
	return tb_async_stream_read_after_(astream, 0, size, func, priv __tb_debug_args__);
}
tb_bool_t tb_async_stream_writ_(tb_async_stream_t* astream, tb_byte_t const* data, tb_size_t size, tb_async_stream_writ_func_t func, tb_pointer_t priv __tb_debug_decl__)
{
	// writ it
	return tb_async_stream_writ_after_(astream, 0, data, size, func, priv __tb_debug_args__);
}
tb_bool_t tb_async_stream_seek_(tb_async_stream_t* astream, tb_hize_t offset, tb_async_stream_seek_func_t func, tb_pointer_t priv __tb_debug_decl__)
{
	// check
	tb_assert_and_check_return_val(astream && astream->seek && func, tb_false);
	
	// check state
	tb_check_return_val(!tb_atomic_get(&astream->base.bstoped), tb_false);
	tb_assert_and_check_return_val(tb_atomic_get(&astream->base.bopened), tb_false);

	// save debug info
#ifdef __tb_debug__
	astream->func = func_;
	astream->file = file_;
	astream->line = line_;
#endif

	// have writed cache? sync it first
	if (astream->wcache_maxn && tb_scoped_buffer_size(&astream->wcache_data))
	{
		// init sync and seek
		astream->sync_and.seek.func = func;
		astream->sync_and.seek.priv = priv;
		astream->sync_and.seek.offset = offset;
		return tb_async_stream_sync_(astream, tb_false, tb_async_stream_sseek_func, &astream->sync_and.seek __tb_debug_args__);
	}

	// offset be not modified?
	if (tb_stream_offset(astream) == offset)
	{
		func(astream, TB_STATE_OK, offset, priv);
		return tb_true;
	}

	// seek it
	return astream->seek(astream, offset, func, priv);
}
tb_bool_t tb_async_stream_sync_(tb_async_stream_t* astream, tb_bool_t bclosing, tb_async_stream_sync_func_t func, tb_pointer_t priv __tb_debug_decl__)
{
	// check
	tb_assert_and_check_return_val(astream && astream->sync && func, tb_false);
	
	// check state
	tb_check_return_val(!tb_atomic_get(&astream->base.bstoped), tb_false);
	tb_assert_and_check_return_val(tb_atomic_get(&astream->base.bopened), tb_false);

	// save debug info
#ifdef __tb_debug__
	astream->func = func_;
	astream->file = file_;
	astream->line = line_;
#endif
 	
	// using cache?
	tb_bool_t ok = tb_false;
	if (astream->wcache_maxn)
	{
		// sync the cache data 
		tb_byte_t* 	data = tb_scoped_buffer_data(&astream->wcache_data);
		tb_size_t 	size = tb_scoped_buffer_size(&astream->wcache_data);
		if (data && size)
		{
			// writ the cache data
			astream->wcache_and.sync.func 		= func;
			astream->wcache_and.sync.bclosing 	= bclosing;
			ok = astream->writ(astream, 0, data, size, tb_async_stream_csync_func, priv);
		}
		// sync it
		else ok = astream->sync(astream, bclosing, func, priv);
	}
	// sync it
	else ok = astream->sync(astream, bclosing, func, priv);

	// ok?
	return ok;
}
tb_bool_t tb_async_stream_task_(tb_async_stream_t* astream, tb_size_t delay, tb_async_stream_task_func_t func, tb_pointer_t priv __tb_debug_decl__)
{
	// check
	tb_assert_and_check_return_val(astream && astream->task && func, tb_false);
	
	// check state
	tb_check_return_val(!tb_atomic_get(&astream->base.bstoped), tb_false);
	tb_assert_and_check_return_val(tb_atomic_get(&astream->base.bopened), tb_false);

	// save debug info
#ifdef __tb_debug__
	astream->func = func_;
	astream->file = file_;
	astream->line = line_;
#endif
 
	// task it
	return astream->task(astream, delay, func, priv);
}
tb_bool_t tb_async_stream_oread_(tb_async_stream_t* astream, tb_size_t size, tb_async_stream_read_func_t func, tb_pointer_t priv __tb_debug_decl__)
{
	// check
	tb_assert_and_check_return_val(astream && astream->open && astream->read && func, tb_false);

	// no opened? open it first
	if (!tb_atomic_get(&astream->base.bopened))
	{
		// init open and read
		astream->open_and.read.func = func;
		astream->open_and.read.priv = priv;
		astream->open_and.read.size = size;
		return tb_async_stream_open_(astream, tb_async_stream_oread_func, &astream->open_and.read __tb_debug_args__);
	}

	// read it
	return tb_async_stream_read_(astream, size, func, priv __tb_debug_args__);
}
tb_bool_t tb_async_stream_owrit_(tb_async_stream_t* astream, tb_byte_t const* data, tb_size_t size, tb_async_stream_writ_func_t func, tb_pointer_t priv __tb_debug_decl__)
{
	// check
	tb_assert_and_check_return_val(astream && astream->open && astream->writ && data && size && func, tb_false);

	// no opened? open it first
	if (!tb_atomic_get(&astream->base.bopened))
	{
		// init open and writ
		astream->open_and.writ.func = func;
		astream->open_and.writ.priv = priv;
		astream->open_and.writ.data = data;
		astream->open_and.writ.size = size;
		return tb_async_stream_open_(astream, tb_async_stream_owrit_func, &astream->open_and.writ __tb_debug_args__);
	}

	// writ it
	return tb_async_stream_writ_(astream, data, size, func, priv __tb_debug_args__);
}
tb_bool_t tb_async_stream_oseek_(tb_async_stream_t* astream, tb_hize_t offset, tb_async_stream_seek_func_t func, tb_pointer_t priv __tb_debug_decl__)
{
	// check
	tb_assert_and_check_return_val(astream && astream->open && astream->seek && func, tb_false);

	// no opened? open it first
	if (!tb_atomic_get(&astream->base.bopened))
	{
		// init open and seek
		astream->open_and.seek.func = func;
		astream->open_and.seek.priv = priv;
		astream->open_and.seek.offset = offset;
		return tb_async_stream_open_(astream, tb_async_stream_oseek_func, &astream->open_and.seek __tb_debug_args__);
	}

	// seek it
	return tb_async_stream_seek_(astream, offset, func, priv __tb_debug_args__);
}
tb_bool_t tb_async_stream_read_after_(tb_async_stream_t* astream, tb_size_t delay, tb_size_t size, tb_async_stream_read_func_t func, tb_pointer_t priv __tb_debug_decl__)
{
	// check
	tb_assert_and_check_return_val(astream && astream->read && func, tb_false);
	
	// check state
	tb_check_return_val(!tb_atomic_get(&astream->base.bstoped), tb_false);
	tb_assert_and_check_return_val(tb_atomic_get(&astream->base.bopened), tb_false);

	// save debug info
#ifdef __tb_debug__
	astream->func = func_;
	astream->file = file_;
	astream->line = line_;
#endif

	// have writed cache? sync it first
	if (astream->wcache_maxn && tb_scoped_buffer_size(&astream->wcache_data))
	{
		// init sync and read
		astream->sync_and.read.func = func;
		astream->sync_and.read.priv = priv;
		astream->sync_and.read.size = size;
		return tb_async_stream_sync_(astream, tb_false, tb_async_stream_sread_func, &astream->sync_and.read __tb_debug_args__);
	}

	// read it
	return tb_async_stream_cread_done(astream, delay, size, func, priv);
}
tb_bool_t tb_async_stream_writ_after_(tb_async_stream_t* astream, tb_size_t delay, tb_byte_t const* data, tb_size_t size, tb_async_stream_writ_func_t func, tb_pointer_t priv __tb_debug_decl__)
{
	// check
	tb_assert_and_check_return_val(astream && astream->writ && data && size && func, tb_false);
	
	// check state
	tb_check_return_val(!tb_atomic_get(&astream->base.bstoped), tb_false);
	tb_assert_and_check_return_val(tb_atomic_get(&astream->base.bopened), tb_false);

	// save debug info
#ifdef __tb_debug__
	astream->func = func_;
	astream->file = file_;
	astream->line = line_;
#endif

	// writ it 
	return tb_async_stream_cwrit_done(astream, delay, data, size, func, priv);
}
tb_aicp_t* tb_async_stream_aicp(tb_async_stream_t* astream)
{
	// check
	tb_assert_and_check_return_val(astream, tb_null);

	// the aicp
	return astream->aicp;
}
#ifdef __tb_debug__
tb_char_t const* tb_async_stream_func(tb_async_stream_t* astream)
{
	// check
	tb_assert_and_check_return_val(astream, tb_null);

	// the func
	return astream->func;
}
tb_char_t const* tb_async_stream_file(tb_async_stream_t* astream)
{
	// check
	tb_assert_and_check_return_val(astream, tb_null);

	// the file
	return astream->file;
}
tb_size_t tb_async_stream_line(tb_async_stream_t* astream)
{
	// check
	tb_assert_and_check_return_val(astream, 0);

	// the line
	return astream->line;
}
#endif