/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  (c) XNeur Team 2006
 *
 */

#include <X11/keysym.h>

#include <strings.h>
#include <string.h>
#include <stdlib.h>

#include "config_main.h"

#include "xutils.h"
#include "xstring.h"
#include "xfocus.h"
#include "xswitchlang.h"
#include "xselection.h"
#include "xbtable.h"
#include "xevent.h"
#include "xwindow.h"
#include "xdefines.h"

#include "utils.h"
#include "text.h"
#include "log.h"
#include "types.h"
#include "detection.h"
#include "conversion.h"

#include "xprogram.h"

#define KLB_NO_ACTION           0 // Modifier, function etc
#define KLB_ADD_SYM             1 // Alpha
#define KLB_DEL_SYM             2 // Backspace
#define KLB_SPACE               3 // Word end (space etc)
#define KLB_ENTER               4 // Enter
#define KLB_CLEAR               5 // Home, End etc

#define MANUAL_FLAG_UNSET	0
#define MANUAL_FLAG_SET		1
#define MANUAL_FLAG_NEED_FLUSH	2

extern struct _xneur_config *xconfig;

struct _xwindow *main_window;

void close_program(void)
{
	main_window->move_window(main_window, 10, 10);		// Simple way to unstuck main event cycle
}

static int get_auto_action(KeySym key, KeySym modifier)
{
	// Cursor keys
	if (IsCursorKey(key))
		return KLB_CLEAR;
	
	// KeyPad keys
	if (IsKeypadKey(key))
	{
		if (get_key_state(XK_Num_Lock) != 0)
		{
			if (modifier == XK_Control_R || modifier == XK_Alt_R || modifier == XK_Shift_R)
				return KLB_CLEAR;
			return KLB_ADD_SYM;
		}
		
		switch (key)
		{
			case XK_KP_Divide:
			case XK_KP_Multiply:
			case XK_KP_Add:
			case XK_KP_Subtract:
				return KLB_ADD_SYM;
		}	
		
		return KLB_CLEAR;
		//
	}

	// Func, Mod, PF, PrivateKeypad keys
	if (IsFunctionKey(key) || IsModifierKey(key) || IsPFKey(key) || IsPrivateKeypadKey(key))
		return KLB_NO_ACTION;

	// MiscFunc keys
	if (IsMiscFunctionKey(key))
	{
		if (key == XK_Insert)
		{
			if (modifier == XK_Control_R || modifier == XK_Alt_R || modifier == XK_Shift_R)
				return KLB_CLEAR;
		}
		return KLB_NO_ACTION;
	}

	// Del, bkspace, tab, return, alpha & num keys
	switch (key)
	{
		case XK_BackSpace:
			return KLB_DEL_SYM;
		case XK_Pause:
		case XK_Escape:
		case XK_Sys_Req:
		case XK_Delete:
			return KLB_NO_ACTION;
		case XK_Return:
			return KLB_ENTER;
		case XK_Tab:
		case XK_space:
		case XK_exclam:
		case XK_at:
		case XK_numbersign:
		case XK_ssharp:
		case XK_dollar:
		case XK_percent:
		case XK_asciicircum:
		case XK_ampersand:
		case XK_asterisk:
		case XK_parenleft:
		case XK_parenright:
		case XK_hyphen:
		case XK_underscore:
		case XK_equal:
		case XK_plus:
		case XK_minus:
		case XK_slash:
		case XK_bar:
		case XK_backslash:
		case XK_question:
			return KLB_SPACE;
	}

	if (modifier == XK_Control_R || modifier == XK_Alt_R)
		return KLB_NO_ACTION;

	return KLB_ADD_SYM;
}

void xprogram_update(struct _xprogram *p, int *do_update)
{
	if (*do_update == FALSE)
		return;

	*do_update = FALSE;

	int status = p->focus->get_focus_status(p->focus);
	if (status != FOCUS_UNCHANGED)
		p->string->clear(p->string);

	if (status == FOCUS_NONE)
         	return;

	int listen_mode = LISTEN_GRAB_INPUT;
	if (status == FOCUS_EXCLUDED)
		listen_mode = LISTEN_DONTGRAB_INPUT;

	p->event->set_owner_window(p->event, p->focus->owner_window);
	p->focus->update_events(p->focus, listen_mode);
}

void xprogram_process_input(struct _xprogram *p)
{
	struct _xevent *event = p->event;

	int do_update = TRUE;
	p->update(p, &do_update);

	while (1)
	{
		int type = event->get_next_event(p->event);

		switch (type)
		{
			case KeyPress:
			{
				p->update(p, &do_update);
				p->on_key_press(p);
				break;
			}
			case EnterNotify:
			case LeaveNotify:
			case FocusIn:
			case FocusOut:
			{
				do_update = TRUE;

				if (XPending(main_window->display))
					break;

				p->update(p, &do_update);
				break;
			}
			case ConfigureNotify:
			{
				if (event->event.xany.window != main_window->window)
					continue;

				log_message(LOG, "Exitting from main cycle");
				return;
			}
			case SelectionNotify:
			{
				p->update(p, &do_update);
				p->convert_selection(p);
				break;
			}
			case ButtonPress:				// Falling down
				p->string->clear(p->string);
			case ButtonRelease:
			case MotionNotify:
			{
				p->update(p, &do_update);
				p->focus->send_button_event(p->focus, &event->event);
				break;
			}
			default:
			{
				log_message(DEBUG, "Uncatched event with type %d\n", type);
				break;
			}
		}
	}
}

void xprogram_convert_selection(struct _xprogram *p)
{
	char *selected_text = get_selected_text(&p->event->event.xselection);
	if (!selected_text)
		return;

	int cur_lang = p->switchlang->update_cur_lang(p->switchlang);

	convert_text_to_ascii(selected_text);

	p->string->set_content(p->string, selected_text);

	p->send_string_silent(p, get_next_lang(cur_lang), FALSE);

	XSetSelectionOwner(main_window->display, p->selection->selection, None, CurrentTime);
	XFree(selected_text);
}

void xprogram_on_key_press(struct _xprogram *p)
{
	KeySym key	= p->event->get_cur_keysym(p->event);
	KeySym modifier	= p->event->get_cur_modifier(p->event);

	enum _hotkey_action manual_action = get_manual_action(key, modifier);
	if (p->perform_manual_action(p, manual_action))
		return;

	int auto_action = get_auto_action(key, modifier);
	p->perform_auto_action(p, auto_action);
}

void xprogram_perform_auto_action(struct _xprogram *p, int action)
{
	struct _xstring *string = p->string;

	switch (action)
	{
		case KLB_CLEAR:
		{
			string->clear(string);
			return;
		}
		case KLB_DEL_SYM:
		{
			string->del_symbol(string);
			return;
		}
		case KLB_SPACE:
		case KLB_ENTER:
		case KLB_ADD_SYM:
		{
			char sym;
			if (action != KLB_ENTER)
				sym = p->event->get_cur_char(p->event);
			else
				sym = ' ';

			if (p->last_action == KLB_ENTER)
				string->clear(string);

			string->add_symbol(string, sym);

			p->last_action = action;

			if (action == KLB_ADD_SYM)
			{
				if (p->changed_manual == MANUAL_FLAG_NEED_FLUSH)
					p->changed_manual = MANUAL_FLAG_UNSET;
				return;
			}

			if (p->changed_manual != MANUAL_FLAG_UNSET)		// Only when Enter or Space is pressed
			{
				p->changed_manual = MANUAL_FLAG_NEED_FLUSH;
				return;
			}

			p->check_last_word(p);
			return;
		}
	}
}

int xprogram_perform_manual_action(struct _xprogram *p, enum _hotkey_action action)
{
	switch (action)
	{
		case ACTION_NONE:
			return FALSE;
		case ACTION_CHANGE_MODE:	// User needs to change current work mode
		{
			if (xconfig->current_mode == AUTO_MODE)
			{
				xconfig->current_mode = MANUAL_MODE;
				log_message(DEBUG, "Current mode changed to manual");
			}
			else
			{
				xconfig->current_mode = AUTO_MODE;
				log_message(DEBUG, "Current mode changed to auto");
			}
			return TRUE;
		}
		case ACTION_CHANGE_SELECTED:
		{
			log_message(DEBUG, "Change selected text");
			p->selection->request_selection(p->selection);
			return TRUE;
		}
		case ACTION_CHANGE_STRING:	// User needs to change current string
		{
			p->send_string_silent(p, get_next_lang(p->switchlang->cur_lang), TRUE);
			break;
		}
		case ACTION_CHANGE_WORD:	// User needs to cancel last change
		{
			p->change_word(p, get_next_lang(p->switchlang->cur_lang));
			break;
		}
	}

	// When CHANGE_STRING or CHANGE_WORD actions occured
	if (p->string->is_space_last(p->string) != TRUE)
		p->changed_manual = MANUAL_FLAG_SET;

	return TRUE;
}

void xprogram_check_last_word(struct _xprogram *p)
{
	if (xconfig->current_mode == MANUAL_MODE)
		return;

	const char *word = get_last_word(p->string->content);
	if (!word)
		return;

	int cur_lang = p->switchlang->update_cur_lang(p->switchlang);

	int new_lang = get_word_lang(word, cur_lang);
	if (new_lang == NO_LANGUAGE)
	{
		log_message(DEBUG, "No language found to change to");
		return;
	}

	if (new_lang == cur_lang)
		return;

	p->change_word(p, new_lang);
}

void xprogram_send_string_silent(struct _xprogram *p, int lang, int send_backspaces)
{
	if (p->string->cur_pos == 0)
	{
		log_message(DEBUG, "No string to change");
		return;
	}

	int cur_lang = p->switchlang->cur_lang;

	log_message(DEBUG, "Processing string '%s'", p->string->content);
	log_message(DEBUG, "Changing language from %s to %s", xconfig->get_lang_name(xconfig, cur_lang), xconfig->get_lang_name(xconfig, lang));

	int bcount = p->string->cur_pos;
	if (send_backspaces == FALSE)
		bcount = 1;

	p->string->set_key_code(p->string, lang);			// Convert string
	p->switchlang->switch_lang(p->switchlang, lang);		// Switch current language

	p->focus->update_events(p->focus, LISTEN_FLUSH);		// Disable receiving events

	p->event->send_backspaces(p->event, bcount);			// Delete old string
	p->event->send_string(p->event, p->string);			// Send new string

	int do_update = TRUE;
	p->update(p, &do_update);
}

void xprogram_change_word(struct _xprogram *p, int lang)
{
	int offset = get_last_word_offset(p->string->content, p->string->cur_pos);

	// Shift fields to point to begin of word
	p->string->content		+= offset;
	p->string->keycode		+= offset;
	p->string->keycode_modifiers	+= offset;
	p->string->cur_pos		-= offset;

	p->send_string_silent(p, lang, TRUE);

	// Revert fields back
	p->string->content		-= offset;
	p->string->keycode		-= offset;
	p->string->keycode_modifiers	-= offset;
	p->string->cur_pos		+= offset;
}

void xprogram_uninit(struct _xprogram *p)
{
	main_window->destroy(main_window);

	p->selection->uninit(p->selection);
	p->event->uninit(p->event);
	p->focus->uninit(p->focus);
	p->switchlang->uninit(p->switchlang);
	p->string->uninit(p->string);

	free(p);
}

struct _xprogram* xprogram_init(void)
{
	struct _xprogram *p = (struct _xprogram*) xnmalloc(sizeof(struct _xprogram));
	bzero(p, sizeof(struct _xprogram));
  
	main_window = xwindow_init();
	if (!main_window->create(main_window))
		return NULL;

	if (!check_keyboard_groups())
		return NULL;

	bind_manual_actions();

	p->selection			= xselection_init();		// X Selection processor
	p->switchlang			= xswitchlang_init();		// KBD-based layout switch
	p->event			= xevent_init();		// X Event processor
	p->focus			= xfocus_init();		// X Input Focus and Pointer processor
	p->string			= xstring_init();		// Input string buffer
  
	// Function mapping
	p->uninit			= xprogram_uninit;
	p->update			= xprogram_update;
	p->on_key_press			= xprogram_on_key_press;
	p->process_input		= xprogram_process_input;
	p->perform_auto_action		= xprogram_perform_auto_action;
	p->perform_manual_action	= xprogram_perform_manual_action;
	p->check_last_word		= xprogram_check_last_word;
	p->change_word			= xprogram_change_word;
	p->convert_selection		= xprogram_convert_selection;
	p->send_string_silent		= xprogram_send_string_silent;

	return p;
}
