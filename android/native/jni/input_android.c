/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2013 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2013 - Daniel De Matteis
 * 
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include <android/keycodes.h>
#include <unistd.h>
#include "android_general.h"
#include "input_autodetect.h"
#include "../../../input/input_common.h"
#include "../../../performance.h"
#include "../../../general.h"
#include "../../../driver.h"

#define MAX_TOUCH 8

#define PRESSED_UP(x, y)   ((-0.80f > y) && (x >= -1.00f))
#define PRESSED_DOWN(x, y) ((0.80f  < y) && (y <= 1.00f))
#define PRESSED_LEFT(x, y) ((-0.80f > x) && (x >= -1.00f))
#define PRESSED_RIGHT(x, y) ((0.80f  < x) && (x <= 1.00f))

#define MAX_DEVICE_IDS 0x100

static unsigned pads_connected;
static uint64_t state[MAX_PADS];
static int8_t state_device_ids[MAX_DEVICE_IDS];

struct input_pointer
{
   int16_t x, y;
};

static struct input_pointer pointer[MAX_TOUCH];
static unsigned pointer_count;

static void *android_input_init(void)
{
   pads_connected = 0;

   for(unsigned player = 0; player < 4; player++)
      for(unsigned i = 0; i < RARCH_FIRST_META_KEY; i++)
      {
         g_settings.input.binds[player][i].id = i;
         g_settings.input.binds[player][i].joykey = 0;
      }

   for(int player = 0; player < 4; player++)
   {
      g_settings.input.binds[player][RETRO_DEVICE_ID_JOYPAD_B].joykey = (1ULL << RETRO_DEVICE_ID_JOYPAD_B);
      g_settings.input.binds[player][RETRO_DEVICE_ID_JOYPAD_Y].joykey = (1ULL << RETRO_DEVICE_ID_JOYPAD_Y);
      g_settings.input.binds[player][RETRO_DEVICE_ID_JOYPAD_SELECT].joykey = (1ULL << RETRO_DEVICE_ID_JOYPAD_SELECT);
      g_settings.input.binds[player][RETRO_DEVICE_ID_JOYPAD_START].joykey = (1ULL << RETRO_DEVICE_ID_JOYPAD_START);
      g_settings.input.binds[player][RETRO_DEVICE_ID_JOYPAD_UP].joykey = (1ULL << RETRO_DEVICE_ID_JOYPAD_UP);
      g_settings.input.binds[player][RETRO_DEVICE_ID_JOYPAD_DOWN].joykey = (1ULL << RETRO_DEVICE_ID_JOYPAD_DOWN);
      g_settings.input.binds[player][RETRO_DEVICE_ID_JOYPAD_LEFT].joykey = (1ULL << RETRO_DEVICE_ID_JOYPAD_LEFT);
      g_settings.input.binds[player][RETRO_DEVICE_ID_JOYPAD_RIGHT].joykey = (1ULL << RETRO_DEVICE_ID_JOYPAD_RIGHT);
      g_settings.input.binds[player][RETRO_DEVICE_ID_JOYPAD_A].joykey = (1ULL << RETRO_DEVICE_ID_JOYPAD_A);
      g_settings.input.binds[player][RETRO_DEVICE_ID_JOYPAD_X].joykey = (1ULL << RETRO_DEVICE_ID_JOYPAD_X);
      g_settings.input.binds[player][RETRO_DEVICE_ID_JOYPAD_L].joykey = (1ULL << RETRO_DEVICE_ID_JOYPAD_L);
      g_settings.input.binds[player][RETRO_DEVICE_ID_JOYPAD_R].joykey = (1ULL << RETRO_DEVICE_ID_JOYPAD_R);
      g_settings.input.binds[player][RETRO_DEVICE_ID_JOYPAD_L2].joykey = (1ULL << RETRO_DEVICE_ID_JOYPAD_L2);
      g_settings.input.binds[player][RETRO_DEVICE_ID_JOYPAD_R2].joykey = (1ULL << RETRO_DEVICE_ID_JOYPAD_R2);
      g_settings.input.binds[player][RETRO_DEVICE_ID_JOYPAD_L3].joykey = (1ULL << RETRO_DEVICE_ID_JOYPAD_L3);
      g_settings.input.binds[player][RETRO_DEVICE_ID_JOYPAD_R3].joykey = (1ULL << RETRO_DEVICE_ID_JOYPAD_R3);
   }

   for(int i = 0; i < MAX_DEVICE_IDS; i++)
      state_device_ids[i] = -1;

   input_autodetect_init();

   return (void*)-1;
}


static void android_input_poll(void *data)
{
   (void)data;

   RARCH_PERFORMANCE_INIT(input_poll);
   RARCH_PERFORMANCE_START(input_poll);

   struct android_app* android_app = g_android.app;

   g_extern.lifecycle_state &= ~((1ULL << RARCH_RESET) | (1ULL << RARCH_REWIND) | (1ULL << RARCH_FAST_FORWARD_KEY) | (1ULL << RARCH_FAST_FORWARD_HOLD_KEY) | (1ULL << RARCH_MUTE) | (1ULL << RARCH_SAVE_STATE_KEY) | (1ULL << RARCH_LOAD_STATE_KEY) | (1ULL << RARCH_STATE_SLOT_PLUS) | (1ULL << RARCH_STATE_SLOT_MINUS));

   // Read all pending events.
   while(AInputQueue_hasEvents(android_app->inputQueue))
   {
      AInputEvent* event = NULL;
      AInputQueue_getEvent(android_app->inputQueue, &event);

      int32_t handled = 1;

      int source = AInputEvent_getSource(event);
      int id = AInputEvent_getDeviceId(event);

      // Some Android devices tend to have really high values for device ID. E.g. 0 and 0x10001.
      // We assume that the lower 8 bits of ID are unique.
      int lut_id = id & 0xff;

      int type_event = AInputEvent_getType(event);
      int state_id = state_device_ids[lut_id];

      if(state_id == -1)
      {
         state_id = state_device_ids[lut_id] = pads_connected++;
         input_autodetect_setup(state_id, id, source);
      }

      int action = 0;
#ifdef RARCH_INPUT_DEBUG
      char msg[128];
#endif

      if(type_event == AINPUT_EVENT_TYPE_MOTION && (g_settings.input.dpad_emulation[state_id] != DPAD_EMULATION_NONE))
      {
         float x = 0.0f;
         float y = 0.0f;
         action = AMotionEvent_getAction(event);
         size_t motion_pointer = action >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
         action &= AMOTION_EVENT_ACTION_MASK;

         if(source & ~(AINPUT_SOURCE_TOUCHSCREEN | AINPUT_SOURCE_MOUSE))
         {
            x = AMotionEvent_getX(event, motion_pointer);
            y = AMotionEvent_getY(event, motion_pointer);
            state[state_id] &= ~((1ULL << RETRO_DEVICE_ID_JOYPAD_LEFT) | (1ULL << RETRO_DEVICE_ID_JOYPAD_RIGHT) |
                  (1ULL << RETRO_DEVICE_ID_JOYPAD_UP) | (1ULL << RETRO_DEVICE_ID_JOYPAD_DOWN));
            state[state_id] |= PRESSED_LEFT(x, y)  ? (1ULL << RETRO_DEVICE_ID_JOYPAD_LEFT)  : 0;
            state[state_id] |= PRESSED_RIGHT(x, y) ? (1ULL << RETRO_DEVICE_ID_JOYPAD_RIGHT) : 0;
            state[state_id] |= PRESSED_UP(x, y)    ? (1ULL << RETRO_DEVICE_ID_JOYPAD_UP)    : 0;
            state[state_id] |= PRESSED_DOWN(x, y)  ? (1ULL << RETRO_DEVICE_ID_JOYPAD_DOWN)  : 0;
         }
         else
         {
            bool keyup = (action == AMOTION_EVENT_ACTION_UP ||
                  action == AMOTION_EVENT_ACTION_CANCEL || action == AMOTION_EVENT_ACTION_POINTER_UP) ||
               (source == AINPUT_SOURCE_MOUSE && action != AMOTION_EVENT_ACTION_DOWN);

            if (!keyup)
            {
               x = AMotionEvent_getX(event, motion_pointer);
               y = AMotionEvent_getY(event, motion_pointer);

               input_translate_coord_viewport(x, y,
                     &pointer[motion_pointer].x, &pointer[motion_pointer].y);

               pointer_count = max(pointer_count, motion_pointer + 1);
            }
            else
            {
               memmove(pointer + motion_pointer, pointer + motion_pointer + 1, (MAX_TOUCH - motion_pointer - 1) * sizeof(struct input_pointer));
               pointer_count--;
            }
         }
#ifdef RARCH_INPUT_DEBUG
         snprintf(msg, sizeof(msg), "Pad %d : x = %.2f, y = %.2f, src %d.\n", state_id, x, y, source);
#endif
      }
      else if (type_event == AINPUT_EVENT_TYPE_KEY)
      {
         int keycode = AKeyEvent_getKeyCode(event);
#ifdef RARCH_INPUT_DEBUG
         snprintf(msg, sizeof(msg), "Pad %d : %d, ac = %d, src = %d.\n", state_id, keycode, action, source);
#endif

         /* Hack - we have to decrease the unpacked value by 1
          * because we 'added' 1 to each entry in the LUT -
          * RETRO_DEVICE_ID_JOYPAD_B is 0
          */
         uint8_t unpacked = (keycode_lut[keycode] >> ((state_id+1) << 3)) - 1;
         uint64_t input_state = (1ULL << unpacked);
         int action  = AKeyEvent_getAction(event);
         uint64_t *key = NULL;

         if(input_state < (1ULL << RARCH_FIRST_META_KEY))
            key = &state[state_id];
         else if(input_state)
            key = &g_extern.lifecycle_state;

         if(key != NULL)
         {
            if (action == AKEY_EVENT_ACTION_UP)
               *key &= ~(input_state);
            else if (action == AKEY_EVENT_ACTION_DOWN)
               *key |= input_state;
         }

         if(volume_enable && (keycode == AKEYCODE_VOLUME_UP || keycode == AKEYCODE_VOLUME_DOWN))
            handled = 0;
      }
#ifdef RARCH_INPUT_DEBUG
      msg_queue_push(g_extern.msg_queue, msg, 0, 30);
#endif
      AInputQueue_finishEvent(android_app->inputQueue, event, handled);
   }

#if 0
   {
      char msg[64];
      snprintf(msg, sizeof(msg), "Pointers: %u", pointer_count);
      msg_queue_clear(g_extern.msg_queue);
      msg_queue_push(g_extern.msg_queue, msg, 0, 30);
   }
#endif

   RARCH_PERFORMANCE_STOP(input_poll);
}

static int16_t android_input_state(void *data, const struct retro_keybind **binds, unsigned port, unsigned device, unsigned index, unsigned id)
{
   switch (device)
   {
      case RETRO_DEVICE_JOYPAD:
         return ((state[port] & binds[port][id].joykey) && (port < pads_connected));
      case RETRO_DEVICE_POINTER:
         switch(id)
         {
            case RETRO_DEVICE_ID_POINTER_X:
               return pointer[index].x;
            case RETRO_DEVICE_ID_POINTER_Y:
               return pointer[index].y;
            case RETRO_DEVICE_ID_POINTER_PRESSED:
               return index < pointer_count;
            default:
               return 0;
         }
      default:
         return 0;
   }
}

static bool android_input_key_pressed(void *data, int key)
{
   return ((g_extern.lifecycle_state | driver.overlay_state) & (1ULL << key));
}

static void android_input_free_input(void *data)
{
   (void)data;
}

const input_driver_t input_android = {
   android_input_init,
   android_input_poll,
   android_input_state,
   android_input_key_pressed,
   android_input_free_input,
   "android_input",
};
