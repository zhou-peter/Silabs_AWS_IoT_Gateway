/***********************************************************************************************//**
 * \file   main.c
 * \brief  Silicon Labs Bluetooth mesh light switch example
 *
 * This example implements a Bluetooth mesh light switch.
 *
 ***************************************************************************************************
 * <b> (C) Copyright 2017 Silicon Labs, http://www.silabs.com</b>
 ***************************************************************************************************
 * This file is licensed under the Silabs License Agreement. See the file
 * "Silabs_License_Agreement.txt" for details. Before using this software for
 * any purpose, you must agree to the terms of that agreement.
 **************************************************************************************************/

/* C Standard Library headers */
#include <stdlib.h>
#include <stdio.h>

/* Board headers */
#include "init_mcu.h"
#include "init_board.h"
#include "init_app.h"
#include "ble-configuration.h"
#include "board_features.h"
#include "retargetserial.h"

/* Bluetooth stack headers */
#include "bg_types.h"
#include "native_gecko.h"
#include "gatt_db.h"
#include <gecko_configuration.h>
#include "mesh_generic_model_capi_types.h"
#include "mesh_lighting_model_capi_types.h"
#include "mesh_lib.h"
#include <mesh_sizes.h>

/* Libraries containing default Gecko configuration values */
#include "em_emu.h"
#include "em_cmu.h"
#include <em_gpio.h>
#include <em_rtcc.h>
#include <gpiointerrupt.h>

/* Device initialization header */
#include "hal-config.h"

/* Display Interface header */
#include "display_interface.h"

#if defined(HAL_CONFIG)
#include "bsphalconfig.h"
#else
#include "bspconfig.h"
#endif

/***********************************************************************************************//**
 * @addtogroup Application
 * @{
 **************************************************************************************************/

/***********************************************************************************************//**
 * @addtogroup app
 * @{
 **************************************************************************************************/

// Maximum number of simultaneous Bluetooth connections
#define MAX_CONNECTIONS 2

// heap for Bluetooth stack
uint8_t bluetooth_stack_heap[DEFAULT_BLUETOOTH_HEAP(MAX_CONNECTIONS) + BTMESH_HEAP_SIZE + 1760];

bool mesh_bgapi_listener(struct gecko_cmd_packet *evt);

// Bluetooth advertisement set configuration
//
// At minimum the following is required:
// * One advertisement set for Bluetooth LE stack (handle number 0)
// * One advertisement set for Mesh data (handle number 1)
// * One advertisement set for Mesh unprovisioned beacons (handle number 2)
// * One advertisement set for Mesh unprovisioned URI (handle number 3)
// * N advertisement sets for Mesh GATT service advertisements
// (one for each network key, handle numbers 4 .. N+3)
//
#define MAX_ADVERTISERS (4 + MESH_CFG_MAX_NETKEYS)

// Bluetooth stack configuration
const gecko_configuration_t config =
{
  .sleep.flags = SLEEP_FLAGS_DEEP_SLEEP_ENABLE,
  .bluetooth.max_connections = MAX_CONNECTIONS,
  .bluetooth.max_advertisers = MAX_ADVERTISERS,
  .bluetooth.heap = bluetooth_stack_heap,
  .bluetooth.heap_size = sizeof(bluetooth_stack_heap) - BTMESH_HEAP_SIZE,
  .bluetooth.sleep_clock_accuracy = 100,
  .gattdb = &bg_gattdb_data,
  .btmesh_heap_size = BTMESH_HEAP_SIZE,
#if (HAL_PA_ENABLE) && defined(FEATURE_PA_HIGH_POWER)
  .pa.config_enable = 1, // Enable high power PA
  .pa.input = GECKO_RADIO_PA_INPUT_VBAT, // Configure PA input to VBAT
#endif // (HAL_PA_ENABLE) && defined(FEATURE_PA_HIGH_POWER)
  .max_timers = 16,
};

// Flag for indicating DFU Reset must be performed
uint8_t boot_to_dfu = 0;

/** Timer Frequency used. */
#define TIMER_CLK_FREQ ((uint32)32768)
/** Convert msec to timer ticks. */
#define TIMER_MS_2_TIMERTICK(ms) ((TIMER_CLK_FREQ * ms) / 1000)

#define TIMER_ID_RESTART    78
#define TIMER_ID_FACTORY_RESET  77
#define TIMER_ID_PROVISIONING   66
#define TIMER_ID_RETRANS    10
#define TIMER_ID_FRIEND_FIND 20

/** Minimum color temperature 800K */
#define TEMPERATURE_MIN      0x0320
/** Maximum color temperature 20000K */
#define TEMPERATURE_MAX      0x4e20

#define DELTA_UV  0

/** global variables */
static uint16 _elem_index = 0xffff; /* For indexing elements of the node (this example has only one element) */
static uint16 _my_address = 0;    /* Address of the Primary Element of the Node */
static uint8 switch_pos = 0;      /* current position of the switch  */
static uint8 request_count;       /* number of on/off requests to be sent */
static uint8 trid = 0;        /* transaction identifier */
static uint8 num_connections = 0;     /* number of active Bluetooth connections */
static uint8 conn_handle = 0xFF;      /* handle of the last opened LE connection */

static uint8 lightness_percent = 0; /* lightness level percentage */
static uint16 lightness_level = 0;  /* lightness level converted from percentage to actual value, range 0..65535*/
static uint8 temperature_percent = 50;
static uint16 temperature_level = 0;

/* button press timestamps for long/short press detection */
static uint32 pb0_press;
static uint32 pb1_press;

#define LONG_PRESS_TIME_TICKS   (32768 / 4)

#define VERY_LONG_PRESS_TIME_TICKS  (32768)

/* external signal definitions. these are used to signal button press events from GPIO interrupt handler to
 * application */
#define EXT_SIGNAL_PB0_SHORT_PRESS      0x01
#define EXT_SIGNAL_PB0_LONG_PRESS       0x02
#define EXT_SIGNAL_PB1_SHORT_PRESS      0x04
#define EXT_SIGNAL_PB1_LONG_PRESS       0x08
#define EXT_SIGNAL_PB0_VERY_LONG_PRESS  0x10
#define EXT_SIGNAL_PB1_VERY_LONG_PRESS  0x20

/**
 *  State of the LEDs is updated by calling LED_set_state().
 *  The new state is passed as parameter, possible values are defined below.
 */
#define LED_STATE_OFF    0   /* light off (both LEDs turned off)   */
#define LED_STATE_ON     1   /* light on (both LEDs turned on)     */
#define LED_STATE_PROV   3   /* provisioning (LEDs blinking)       */

/**
 *  These are needed to support radio boards with active-low and
 *  active-high LED configuration
 */
#ifdef FEATURE_LED_BUTTON_ON_SAME_PIN
/* LED GPIO is active-low */
#define TURN_LED_OFF   GPIO_PinOutSet
#define TURN_LED_ON    GPIO_PinOutClear
#define LED_DEFAULT_STATE  1
#else
/* LED GPIO is active-high */
#define TURN_LED_OFF   GPIO_PinOutClear
#define TURN_LED_ON    GPIO_PinOutSet
#define LED_DEFAULT_STATE  0
#endif

/**
 * Update the state of LEDs. Takes one parameter LED_STATE_xxx that defines
 * the new state.
 */
static void LED_set_state(int state)
{
  switch (state) {
    case LED_STATE_OFF:
      TURN_LED_OFF(BSP_LED0_PORT, BSP_LED0_PIN);
      TURN_LED_OFF(BSP_LED1_PORT, BSP_LED1_PIN);
      break;
    case LED_STATE_ON:
      TURN_LED_ON(BSP_LED0_PORT, BSP_LED0_PIN);
      TURN_LED_ON(BSP_LED1_PORT, BSP_LED1_PIN);
      break;
    case LED_STATE_PROV:
      GPIO_PinOutToggle(BSP_LED0_PORT, BSP_LED0_PIN);
      GPIO_PinOutToggle(BSP_LED1_PORT, BSP_LED1_PIN);
      break;

    default:
      break;
  }
}

/**
 * This is a callback function that is invoked each time a GPIO interrupt in one of the pushbutton
 * inputs occurs. Pin number is passed as parameter.
 *
 * Note: this function is called from ISR context and therefore it is not possible to call any BGAPI
 * functions directly. The button state change is signaled to the application using gecko_external_signal()
 * that will generate an event gecko_evt_system_external_signal_id which is then handled in the main loop.
 */
void gpioint(uint8_t pin)
{
  uint32_t t_diff;

  if (pin == BSP_BUTTON0_PIN) {
    if (GPIO_PinInGet(BSP_BUTTON0_PORT, BSP_BUTTON0_PIN) == 0) {
      // PB0 pressed - record RTCC timestamp
      pb0_press = RTCC_CounterGet();
    } else {
      // PB0 released - check if it was short or long press
      t_diff = RTCC_CounterGet() - pb0_press;
      if (t_diff < LONG_PRESS_TIME_TICKS) {
        gecko_external_signal(EXT_SIGNAL_PB0_SHORT_PRESS);
      } else if (t_diff < VERY_LONG_PRESS_TIME_TICKS) {
        gecko_external_signal(EXT_SIGNAL_PB0_LONG_PRESS);
      } else {
        gecko_external_signal(EXT_SIGNAL_PB0_VERY_LONG_PRESS);
      }
    }
  } else if (pin == BSP_BUTTON1_PIN) {
    if (GPIO_PinInGet(BSP_BUTTON1_PORT, BSP_BUTTON1_PIN) == 0) {
      // PB1 pressed - record RTCC timestamp
      pb1_press = RTCC_CounterGet();
    } else {
      // PB1 released - check if it was short or long press
      t_diff = RTCC_CounterGet() - pb1_press;
      if (t_diff < LONG_PRESS_TIME_TICKS) {
        gecko_external_signal(EXT_SIGNAL_PB1_SHORT_PRESS);
      } else if (t_diff < VERY_LONG_PRESS_TIME_TICKS) {
        gecko_external_signal(EXT_SIGNAL_PB1_LONG_PRESS);
      } else {
        gecko_external_signal(EXT_SIGNAL_PB1_VERY_LONG_PRESS);
      }
    }
  }
}

/**
 * Enable button interrupts for PB0, PB1. Both GPIOs are configured to trigger an interrupt on the
 * rising edge (button released).
 */
void enable_button_interrupts(void)
{
  GPIOINT_Init();

  /* configure interrupt for PB0 and PB1, both falling and rising edges */
  GPIO_ExtIntConfig(BSP_BUTTON0_PORT, BSP_BUTTON0_PIN, BSP_BUTTON0_PIN, true, true, true);
  GPIO_ExtIntConfig(BSP_BUTTON1_PORT, BSP_BUTTON1_PIN, BSP_BUTTON1_PIN, true, true, true);

  /* register the callback function that is invoked when interrupt occurs */
  GPIOINT_CallbackRegister(BSP_BUTTON0_PIN, gpioint);
  GPIOINT_CallbackRegister(BSP_BUTTON1_PIN, gpioint);
}

/**
 * This function publishes one on/off request to change the state of light(s) in the group.
 * Global variable switch_pos holds the latest desired light state, possible values are
 * switch_pos = 1 -> PB1 was pressed, turn lights on
 * switch_pos = 0 -> PB0 was pressed, turn lights off
 *
 * This application sends multiple requests for each button press to improve reliability.
 * Parameter retrans indicates whether this is the first request or a re-transmission.
 * The transaction ID is not incremented in case of a re-transmission.
 */
void send_onoff_request(int retrans)
{
  uint16 resp;
  uint16 delay;
  struct mesh_generic_request req;
  const uint32 transtime = 0; /* using zero transition time by default */

  req.kind = mesh_generic_request_on_off;
  req.on_off = switch_pos ? MESH_GENERIC_ON_OFF_STATE_ON : MESH_GENERIC_ON_OFF_STATE_OFF;

  // increment transaction ID for each request, unless it's a retransmission
  if (retrans == 0) {
    trid++;
  }

  /* delay for the request is calculated so that the last request will have a zero delay and each
   * of the previous request have delay that increases in 50 ms steps. For example, when using three
   * on/off requests per button press the delays are set as 100, 50, 0 ms
   */
  delay = (request_count - 1) * 50;

  resp = gecko_cmd_mesh_generic_client_publish(
    MESH_GENERIC_ON_OFF_CLIENT_MODEL_ID,
    _elem_index,
    trid,
    transtime,   // transition time in ms
    delay,
    0,     // flags
    mesh_generic_request_on_off,     // type
    1,     // param len
    &req.on_off     /// parameters data
    )->result;

  if (resp) {
    printf("gecko_cmd_mesh_generic_client_publish failed,code %x\r\n", resp);
  } else {
    printf("request sent, trid = %u, delay = %d\r\n", trid, delay);
  }

  /* keep track of how many requests has been sent */
  if (request_count > 0) {
    request_count--;
  }
}

void send_lightness_request(int retrans)
{
  uint16 resp;
  uint16 delay;
  struct mesh_generic_request req;

  req.kind = mesh_lighting_request_lightness_actual;
  req.lightness = lightness_level;

  // increment transaction ID for each request, unless it's a retransmission
  if (retrans == 0) {
    trid++;
  }

  delay = 0;

  resp = gecko_cmd_mesh_generic_client_publish(
    MESH_LIGHTING_LIGHTNESS_CLIENT_MODEL_ID,
    _elem_index,
    trid,
    0,     // transition
    delay,
    0,     // flags
    mesh_lighting_request_lightness_actual, // type
    2,     // param len
    (uint8*)&req.lightness   /// parameters data
    //&req.lightness     /// parameters data
    )->result;

  if (resp) {
    printf("gecko_cmd_mesh_generic_client_publish failed,code %x\r\n", resp);
  } else {
    printf("request sent, trid = %u, delay = %d\r\n", trid, delay);
  }
}

void send_ctl_request(int retrans)
{
  uint16 resp;
  uint16 delay;
  struct mesh_generic_request req;

  req.kind = mesh_lighting_request_ctl;
  req.ctl.lightness = lightness_level;
  req.ctl.temperature = temperature_level;
  req.ctl.deltauv = DELTA_UV; //hardcoded delta uv

  // increment transaction ID for each request, unless it's a retransmission
  if (retrans == 0) {
    trid++;
  }

  delay = 0;

  resp = gecko_cmd_mesh_generic_client_publish(
    MESH_LIGHTING_CTL_CLIENT_MODEL_ID,
    _elem_index,
    trid,
    0,     // transition
    delay,
    0,     // flags
    mesh_lighting_request_ctl, // type
    6,     // param len
    (uint8*)&req.ctl   /// parameters data
    )->result;

  if (resp) {
    printf("gecko_cmd_mesh_generic_client_publish failed,code %x\r\n", resp);
  } else {
    printf("request sent, trid = %u, delay = %d\r\n", trid, delay);
  }
}

/**
 * Handling of short button presses. This function called from the main loop when application receives
 * event gecko_evt_system_external_signal_id.
 *
 * parameter button defines which button was pressed, possible values
 * are 0 = PB0, 1 = PB1.
 *
 * This function is called from application context (not ISR) so it is safe to call BGAPI functions
 */
void handle_button_press(int button)
{
  /* short press adjusts light brightness, using Light Lightness model */
  if (button == 1) {
    lightness_percent += 10;
    if (lightness_percent > 100) {
      lightness_percent = 100;
    }
  } else {
    if (lightness_percent >= 10) {
      lightness_percent -= 10;
    }
  }

  lightness_level = lightness_percent * 0xFFFF / 100;
  printf("set light to %d %% / level %d\r\n", lightness_percent, lightness_level);

  /* send the request (lightness request is sent only once in this example) */
  send_lightness_request(0);
}

void handle_long_press(int button)
{
  /* short press adjusts light brightness, using Light Lightness model */
  if (button == 1) {
    temperature_percent += 10;
    if (temperature_percent > 100) {
      temperature_percent = 100;
    }
  } else {
    if (temperature_percent >= 10) {
      temperature_percent -= 10;
    }
  }

  /* using square of percentage to change temperature more uniformly */
  temperature_level = TEMPERATURE_MIN + (temperature_percent * temperature_percent / 100) * (TEMPERATURE_MAX - TEMPERATURE_MIN) / 100;
  printf("set temperature to %d %% / level %d K\r\n", temperature_percent * temperature_percent / 100, temperature_level);

  /* send the request (ctl request is sent only once in this example) */
  send_ctl_request(0);
}

/**
 * Handling of long button presses. This function called from the main loop when application receives
 * event gecko_evt_system_external_signal_id.
 *
 * parameter button defines which button was pressed, possible values
 * are 0 = PB0, 1 = PB1.
 *
 * This function is called from application context (not ISR) so it is safe to call BGAPI functions
 */
void handle_very_long_press(int button)
{
  // PB0 -> switch off, PB1 -> switch on
  switch_pos = button;

  /* long press turns light ON or OFF, using Generic OnOff model */
  printf("PB%d -> turn light(s) ", button);
  if (switch_pos) {
    printf("on\r\n");
    lightness_percent = 100;
  } else {
    printf("off\r\n");
    lightness_percent = 0;
  }

  request_count = 3; // request is sent 3 times to improve reliability

  /* send the first request */
  send_onoff_request(0);

  /* start a repeating soft timer to trigger re-transmission of the request after 50 ms delay */
  gecko_cmd_hardware_set_soft_timer(TIMER_MS_2_TIMERTICK(50), TIMER_ID_RETRANS, 0);
}

/**
 * Initialize LPN functionality with configuration and friendship establishment.
 */
void lpn_init(void)
{
  uint16 res;
  // Initialize LPN functionality.
  res = gecko_cmd_mesh_lpn_init()->result;
  if (res) {
    printf("LPN init failed (0x%x)\r\n", res);
    return;
  }

  res = gecko_cmd_mesh_lpn_configure(2, 5 * 1000)->result;
  if (res) {
    printf("LPN conf failed (0x%x)\r\n", res);
    return;
  }

  printf("trying to find friend...\r\n");
  res = gecko_cmd_mesh_lpn_establish_friendship(0)->result;

  if (res != 0) {
    printf("ret.code %x\r\n", res);
  }
}

/**
 * Switch node initialization. This is called at each boot if provisioning is already done.
 * Otherwise this function is called after provisioning is completed.
 */
void switch_node_init(void)
{
  mesh_lib_init(malloc, free, 8);

  lpn_init();
}

static void handle_gecko_event(uint32_t evt_id, struct gecko_cmd_packet *evt);

/**
 * button initialization. Configure pushbuttons PB0,PB1
 * as inputs.
 */
static void button_init()
{
  // configure pushbutton PB0 and PB1 as inputs, with pull-up enabled
  GPIO_PinModeSet(BSP_BUTTON0_PORT, BSP_BUTTON0_PIN, gpioModeInputPull, 1);
  GPIO_PinModeSet(BSP_BUTTON1_PORT, BSP_BUTTON1_PIN, gpioModeInputPull, 1);
}

/**
 * LED initialization. Configure LED pins as outputs
 */
static void led_init()
{
  // configure LED0 and LED1 as outputs
  GPIO_PinModeSet(BSP_LED0_PORT, BSP_LED0_PIN, gpioModePushPull, LED_DEFAULT_STATE);
  GPIO_PinModeSet(BSP_LED1_PORT, BSP_LED1_PIN, gpioModePushPull, LED_DEFAULT_STATE);
}

/**
 * Set device name in the GATT database. A unique name is genrerated using
 * the two last bytes from the Bluetooth address of this device. Name is also
 * displayed on the LCD.
 */
void set_device_name(bd_addr *pAddr)
{
  char name[20];
  uint16 res;

  // create unique device name using the last two bytes of the Bluetooth address
  sprintf(name, "switch node %x:%x", pAddr->addr[1], pAddr->addr[0]);

  printf("Device name: '%s'\r\n", name);

  res = gecko_cmd_gatt_server_write_attribute_value(gattdb_device_name, 0, strlen(name), (uint8 *)name)->result;
  if (res) {
    printf("gecko_cmd_gatt_server_write_attribute_value() failed, code %x\r\n", res);
  }

  // show device name on the LCD
  DI_Print(name, DI_ROW_NAME);
}

/**
 *  this function is called to initiate factory reset. Factory reset may be initiated
 *  by keeping one of the WSTK pushbuttons pressed during reboot. Factory reset is also
 *  performed if it is requested by the provisioner (event gecko_evt_mesh_node_reset_id)
 */
void initiate_factory_reset(void)
{
  printf("factory reset\r\n");
  DI_Print("\n***\nFACTORY RESET\n***", DI_ROW_STATUS);

  /* if connection is open then close it before rebooting */
  if (conn_handle != 0xFF) {
    gecko_cmd_le_connection_close(conn_handle);
  }

  /* perform a factory reset by erasing PS storage. This removes all the keys and other settings
     that have been configured for this node */
  gecko_cmd_flash_ps_erase_all();
  // reboot after a small delay
  gecko_cmd_hardware_set_soft_timer(2 * 32768, TIMER_ID_FACTORY_RESET, 1);
}

int main()
{
  // Initialize device
  initMcu();
  // Initialize board
  initBoard();
  // Initialize application
  initApp();

  gecko_stack_init(&config);
  gecko_bgapi_class_dfu_init();
  gecko_bgapi_class_system_init();
  gecko_bgapi_class_le_gap_init();
  gecko_bgapi_class_le_connection_init();
  //gecko_bgapi_class_gatt_init();
  gecko_bgapi_class_gatt_server_init();
  gecko_bgapi_class_endpoint_init();
  gecko_bgapi_class_hardware_init();
  gecko_bgapi_class_flash_init();
  gecko_bgapi_class_test_init();
  //gecko_bgapi_class_sm_init();
  //mesh_native_bgapi_init();
  gecko_bgapi_class_mesh_node_init();
  //gecko_bgapi_class_mesh_prov_init();
  gecko_bgapi_class_mesh_proxy_init();
  gecko_bgapi_class_mesh_proxy_server_init();
  //gecko_bgapi_class_mesh_proxy_client_init();
  gecko_bgapi_class_mesh_generic_client_init();
  //gecko_bgapi_class_mesh_generic_server_init();
  //gecko_bgapi_class_mesh_vendor_model_init();
  //gecko_bgapi_class_mesh_health_client_init();
  //gecko_bgapi_class_mesh_health_server_init();
  //gecko_bgapi_class_mesh_test_init();
  gecko_bgapi_class_mesh_lpn_init();
  //gecko_bgapi_class_mesh_friend_init();

  gecko_initCoexHAL();

  RETARGET_SerialInit();

  /* initialize LEDs and buttons. Note: some radio boards share the same GPIO for button & LED.
   * Initialization is done in this order so that default configuration will be "button" for those
   * radio boards with shared pins. led_init() is called later as needed to (re)initialize the LEDs
   * */
  led_init();
  button_init();

  DI_Init();

#if defined(_SILICON_LABS_32B_SERIES_1_CONFIG_3)
  /* xG13 devices have two RTCCs, one for the stack and another for the application.
   * The clock for RTCC needs to be enabled in application code. In xG12 RTCC init
   * is handled by the stack */
  CMU_ClockEnable(cmuClock_RTCC, true);
#endif

  while (1) {
    struct gecko_cmd_packet *evt = gecko_wait_event();
    bool pass = mesh_bgapi_listener(evt);
    if (pass) {
      handle_gecko_event(BGLIB_MSG_ID(evt->header), evt);
    }
  }
}

/**
 * Handling of stack events. Both BLuetooth LE and Bluetooth mesh events are handled here.
 */
static void handle_gecko_event(uint32_t evt_id, struct gecko_cmd_packet *evt)
{
  uint16 result;
  char buf[30];

  struct gecko_msg_mesh_node_provisioning_failed_evt_t  *prov_fail_evt;

  if (NULL == evt) {
    return;
  }

  switch (evt_id) {
    case gecko_evt_system_boot_id:
      // check pushbutton state at startup. If either PB0 or PB1 is held down then do factory reset
      if (GPIO_PinInGet(BSP_BUTTON0_PORT, BSP_BUTTON0_PIN) == 0 || GPIO_PinInGet(BSP_BUTTON1_PORT, BSP_BUTTON1_PIN) == 0) {
        initiate_factory_reset();
      } else {
        struct gecko_msg_system_get_bt_address_rsp_t *pAddr = gecko_cmd_system_get_bt_address();

        set_device_name(&pAddr->address);

        // Initialize Mesh stack in Node operation mode, wait for initialized event
        result = gecko_cmd_mesh_node_init()->result;
        if (result) {
          sprintf(buf, "init failed (0x%x)", result);
          DI_Print(buf, DI_ROW_STATUS);
        }
      }
      break;

    case gecko_evt_hardware_soft_timer_id:
      switch (evt->data.evt_hardware_soft_timer.handle) {
        case TIMER_ID_FACTORY_RESET:
          gecko_cmd_system_reset(0);
          break;

        case TIMER_ID_RESTART:
          gecko_cmd_system_reset(0);
          break;

        case TIMER_ID_PROVISIONING:
          LED_set_state(LED_STATE_PROV);
          break;

        case TIMER_ID_RETRANS:
          send_onoff_request(1);   // param 1 indicates that this is a retransmission
          // stop retransmission timer if it was the last attempt
          if (request_count == 0) {
            gecko_cmd_hardware_set_soft_timer(0, TIMER_ID_RETRANS, 0);
          }
          break;

        case TIMER_ID_FRIEND_FIND:
        {
          printf("trying to find friend...\r\n");
          result = gecko_cmd_mesh_lpn_establish_friendship(0)->result;

          if (result != 0) {
            printf("ret.code %x\r\n", result);
          }
        }
        break;

        default:
          break;
      }

      break;

    case gecko_evt_mesh_node_initialized_id:
      printf("node initialized\r\n");

      gecko_cmd_mesh_generic_client_init();

      struct gecko_msg_mesh_node_initialized_evt_t *pData = (struct gecko_msg_mesh_node_initialized_evt_t *)&(evt->data);

      if (pData->provisioned) {
        printf("node is provisioned. address:%x, ivi:%ld\r\n", pData->address, pData->ivi);

        _my_address = pData->address;
        _elem_index = 0;   // index of primary element is zero. This example has only one element.

        enable_button_interrupts();
        switch_node_init();

        DI_Print("provisioned", DI_ROW_STATUS);
      } else {
        printf("node is unprovisioned\r\n");
        DI_Print("unprovisioned", DI_ROW_STATUS);

        printf("starting unprovisioned beaconing...\r\n");
        //gecko_cmd_mesh_node_start_unprov_beaconing(0x3);   // enable ADV and GATT provisioning bearer
        gecko_cmd_mesh_node_start_unprov_beaconing(0x2);   // enable GATT provisioning bearer
      }
      break;

    case gecko_evt_system_external_signal_id:
    {
      if (evt->data.evt_system_external_signal.extsignals & EXT_SIGNAL_PB0_SHORT_PRESS) {
        handle_button_press(0);
      }

      if (evt->data.evt_system_external_signal.extsignals & EXT_SIGNAL_PB1_SHORT_PRESS) {
        handle_button_press(1);
      }
      if (evt->data.evt_system_external_signal.extsignals & EXT_SIGNAL_PB0_LONG_PRESS) {
        handle_long_press(0);
      }
      if (evt->data.evt_system_external_signal.extsignals & EXT_SIGNAL_PB1_LONG_PRESS) {
        handle_long_press(1);
      }
      if (evt->data.evt_system_external_signal.extsignals & EXT_SIGNAL_PB0_VERY_LONG_PRESS) {
        handle_very_long_press(0);
      }
      if (evt->data.evt_system_external_signal.extsignals & EXT_SIGNAL_PB1_VERY_LONG_PRESS) {
        handle_very_long_press(1);
      }
    }
    break;

    case gecko_evt_mesh_node_provisioning_started_id:
      printf("Started provisioning\r\n");
      DI_Print("provisioning...", DI_ROW_STATUS);
#ifdef FEATURE_LED_BUTTON_ON_SAME_PIN
      led_init(); /* shared GPIO pins used as LED output */
#endif
      // start timer for blinking LEDs to indicate which node is being provisioned
      gecko_cmd_hardware_set_soft_timer(32768 / 4, TIMER_ID_PROVISIONING, 0);
      break;

    case gecko_evt_mesh_node_provisioned_id:
      _elem_index = 0;   // index of primary element is zero. This example has only one element.
      switch_node_init();
      printf("node provisioned, got address=%x\r\n", evt->data.evt_mesh_node_provisioned.address);
      // stop LED blinking when provisioning complete
      gecko_cmd_hardware_set_soft_timer(0, TIMER_ID_PROVISIONING, 0);
      LED_set_state(LED_STATE_OFF);
      DI_Print("provisioned", DI_ROW_STATUS);

#ifdef FEATURE_LED_BUTTON_ON_SAME_PIN
      button_init(); /* shared GPIO pins used as button input */
#endif
      enable_button_interrupts();
      break;

    case gecko_evt_mesh_node_provisioning_failed_id:
      prov_fail_evt = (struct gecko_msg_mesh_node_provisioning_failed_evt_t  *)&(evt->data);
      printf("provisioning failed, code %x\r\n", prov_fail_evt->result);
      DI_Print("prov failed", DI_ROW_STATUS);
      /* start a one-shot timer that will trigger soft reset after small delay */
      gecko_cmd_hardware_set_soft_timer(2 * 32768, TIMER_ID_RESTART, 1);
      break;

    case gecko_evt_mesh_node_key_added_id:
      printf("got new %s key with index %x\r\n", evt->data.evt_mesh_node_key_added.type == 0 ? "network" : "application",
             evt->data.evt_mesh_node_key_added.index);
      break;

    case gecko_evt_mesh_node_model_config_changed_id:
      printf("model config changed\r\n");
      break;

    case gecko_evt_le_connection_opened_id:
      printf("evt:gecko_evt_le_connection_opened_id\r\n");
      num_connections++;
      conn_handle = evt->data.evt_le_connection_opened.connection;
      DI_Print("connected", DI_ROW_CONNECTION);
      // turn off lpn feature after GATT connection is opened
      gecko_cmd_mesh_lpn_deinit();
      DI_Print("LPN off", DI_ROW_LPN);
      break;

    case gecko_evt_le_connection_closed_id:
      /* Check if need to boot to dfu mode */
      if (boot_to_dfu) {
        /* Enter to DFU OTA mode */
        gecko_cmd_system_reset(2);
      }

      printf("evt:conn closed, reason 0x%x\r\n", evt->data.evt_le_connection_closed.reason);
      conn_handle = 0xFF;
      if (num_connections > 0) {
        if (--num_connections == 0) {
          DI_Print("", DI_ROW_CONNECTION);

          lpn_init();
        }
      }
      break;

    case gecko_evt_mesh_node_reset_id:
      printf("evt gecko_evt_mesh_node_reset_id\r\n");
      initiate_factory_reset();
      break;

    case gecko_evt_le_connection_parameters_id:
      printf("connection params: interval %d, timeout %d\r\n", evt->data.evt_le_connection_parameters.interval,
             evt->data.evt_le_connection_parameters.timeout
             );
      break;

    case gecko_evt_le_gap_adv_timeout_id:
      // these events silently discarded
      break;

    case gecko_evt_gatt_server_user_write_request_id:
      if (evt->data.evt_gatt_server_user_write_request.characteristic == gattdb_ota_control) {
        /* Set flag to enter to OTA mode */
        boot_to_dfu = 1;
        /* Send response to Write Request */
        gecko_cmd_gatt_server_send_user_write_response(
          evt->data.evt_gatt_server_user_write_request.connection,
          gattdb_ota_control,
          bg_err_success);

        /* Close connection to enter to DFU OTA mode */
        gecko_cmd_le_connection_close(evt->data.evt_gatt_server_user_write_request.connection);
      }
      break;

    case gecko_evt_mesh_lpn_friendship_established_id:
      printf("friendship established\r\n");
      DI_Print("LPN", DI_ROW_LPN);
      break;

    case gecko_evt_mesh_lpn_friendship_failed_id:
      printf("friendship failed\r\n");
      DI_Print("no friend", DI_ROW_LPN);
      // try again in 2 seconds
      result  = gecko_cmd_hardware_set_soft_timer(TIMER_MS_2_TIMERTICK(2000), TIMER_ID_FRIEND_FIND, 1)->result;
      if (result) {
        printf("timer failure?!  %x\r\n", result);
      }
      break;

    case gecko_evt_mesh_lpn_friendship_terminated_id:
      printf("friendship terminated\r\n");
      DI_Print("friend lost", DI_ROW_LPN);
      if (num_connections == 0) {
        // try again in 2 seconds
        result  = gecko_cmd_hardware_set_soft_timer(TIMER_MS_2_TIMERTICK(2000), TIMER_ID_FRIEND_FIND, 1)->result;
        if (result) {
          printf("timer failure?!  %x\r\n", result);
        }
      }
      break;

    default:
      //printf("unhandled evt: %8.8x class %2.2x method %2.2x\r\n", evt_id, (evt_id >> 16) & 0xFF, (evt_id >> 24) & 0xFF);
      break;
  }
}
