#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <util/delay.h>
#include <avr/wdt.h>
#include <stdbool.h>
#include <avr/pgmspace.h>

/*
 * Gate Controller Firmware for ATmega8535
 * 
 * Controls a relay-based H-bridge motor to open/close a gate using a momentary push button.
 * - A single button press toggles the gate between open and closed.
 * - Pressing the button while the gate is moving will immediately stop it and assume the destination state.
 * - Includes a watchdog timer for crash recovery and optional scheduled resets to maintain long-term stability.
 * 
 * Author: Alex Findlay
 * Date: 2025-03-23
 * License: MIT
 * Repository: https://github.com/lnxd
 */

#define F_CPU 8000000UL
#define BAUD 9600
#define UBRR_VALUE ((F_CPU / (16UL * BAUD)) - 1)

#define WDT_TIMEOUT WDTO_1S          // 1 second timeout
#define REGULAR_RESET_HOURS 6        // Reset every 6 hours if idle
#define MS_PER_HOUR 3600000UL        // Number of milliseconds in an hour

#define GATE_OPERATION_TIME 30000    // 30 seconds for gate to fully open/close
#define RELAY_SWITCHING_DELAY 100    // 100ms delay between relay operations
#define BUTTON_DEBOUNCE_DELAY 250    // 250ms for button debounce
#define SHORT_DELAY 10               // 10ms for short delays in loops
#define RESET_DELAY 5000             // 5 seconds before controlled reset

#define RELAY_K1 PB0
#define RELAY_K2 PB1
#define RELAY_K3 PB2
#define RELAY_K4 PB3
#define BUTTON_PIN PD2
#define LED_OPENING PD4
#define LED_CLOSING PD5

#define EEPROM_ADDR 0x00
#define EEPROM_RESET_FLAG 0x01

#define GATE_CLOSED 0
#define GATE_CLOSING 1
#define GATE_OPENING 2
#define GATE_OPEN 3

void uart_init(void);
void uart_tx_char(char c);
void uart_tx_string(const char* str);
void uart_tx_string_P(const char* str);
void init_watchdog(void);
void reset_watchdog(void);
void schedule_reset(void);
void perform_controlled_reset(void);
void init_io(void);
void init_interrupts(void);
uint8_t read_gate_state(void);
void write_gate_state(uint8_t state);
uint8_t check_reset_flag(void);
void indicate_opening(void);
void indicate_closing(void);
void indicate_stop(void);
void report_state(void);
void stop_gate(void);
void open_gate(void);
void close_gate(void);
void toggle_gate(void);
void emergency_stop(void);

volatile uint8_t gate_state;
volatile uint8_t button_pressed = 0;
volatile uint8_t gate_moving = 0;
volatile uint32_t idle_timer_ms = 0;
volatile uint8_t reset_scheduled = 0;

void uart_init(void) {
    UBRRH = (uint8_t)(UBRR_VALUE >> 8);
    UBRRL = (uint8_t)(UBRR_VALUE);
    UCSRB = (1 << TXEN);
    UCSRC = (1 << URSEL) | (1 << UCSZ1) | (1 << UCSZ0);
}

void uart_tx_char(char c) {
    while (!(UCSRA & (1 << UDRE)));
    UDR = c;
}

void uart_tx_string(const char* str) {
    while (*str) {
        uart_tx_char(*str++);
    }
}

void uart_tx_string_P(const char* str) {
    char c;
    while ((c = pgm_read_byte(str++))) {
        uart_tx_char(c);
    }
}

void init_watchdog(void) {
    wdt_reset();
    wdt_enable(WDT_TIMEOUT);
    uart_tx_string_P(PSTR("Watchdog timer enabled.\r\n"));
}

void reset_watchdog(void) {
    wdt_reset();
}

void schedule_reset(void) {
    if (!gate_moving && !reset_scheduled) {
        uart_tx_string_P(PSTR("Scheduled reset after 6 hours of inactivity\r\n"));
        reset_scheduled = 1;
        idle_timer_ms = 0;
    }
}

void perform_controlled_reset(void) {
    uart_tx_string_P(PSTR("Performing controlled system reset\r\n"));
    
    if (gate_moving) {
        stop_gate();
    }
    
    eeprom_write_byte((uint8_t*)EEPROM_RESET_FLAG, 1);
    
    wdt_enable(WDTO_15MS);
    while(1);
}

void init_io(void) {
    DDRB |= (1 << RELAY_K1) | (1 << RELAY_K2) | (1 << RELAY_K3) | (1 << RELAY_K4);
    DDRD |= (1 << LED_OPENING) | (1 << LED_CLOSING);
    DDRD &= ~(1 << BUTTON_PIN);
    PORTD |= (1 << BUTTON_PIN);
}

void init_interrupts(void) {
    GICR |= (1 << INT0);
    MCUCR |= (1 << ISC01);
    MCUCR &= ~(1 << ISC00);
    sei();
}

uint8_t read_gate_state(void) {
    uint8_t state = eeprom_read_byte((uint8_t*)EEPROM_ADDR);
    if (state == GATE_CLOSING) state = GATE_CLOSED;
    if (state == GATE_OPENING) state = GATE_OPEN;
    return state;
}

void write_gate_state(uint8_t state) {
    if (read_gate_state() != state) {
        eeprom_write_byte((uint8_t*)EEPROM_ADDR, state);
    }
}

uint8_t check_reset_flag(void) {
    uint8_t flag = eeprom_read_byte((uint8_t*)EEPROM_RESET_FLAG);
    if (flag != 0) {
        eeprom_write_byte((uint8_t*)EEPROM_RESET_FLAG, 0);
    }
    return flag;
}

void indicate_opening(void) {
    PORTD |= (1 << LED_OPENING);
    PORTD &= ~(1 << LED_CLOSING);
}

void indicate_closing(void) {
    PORTD |= (1 << LED_CLOSING);
    PORTD &= ~(1 << LED_OPENING);
}

void indicate_stop(void) {
    PORTD &= ~((1 << LED_OPENING) | (1 << LED_CLOSING));
}

void report_state(void) {
    switch (gate_state) {
        case GATE_CLOSED:
            uart_tx_string_P(PSTR("State: Gate Closed\r\n"));
            break;
        case GATE_CLOSING:
            uart_tx_string_P(PSTR("State: Gate Closing\r\n"));
            break;
        case GATE_OPENING:
            uart_tx_string_P(PSTR("State: Gate Opening\r\n"));
            break;
        case GATE_OPEN:
            uart_tx_string_P(PSTR("State: Gate Open\r\n"));
            break;
    }
}

void stop_gate(void) {
    PORTB &= ~((1 << RELAY_K1) | (1 << RELAY_K2) | (1 << RELAY_K3) | (1 << RELAY_K4));
    indicate_stop();
    gate_moving = 0;
}

void open_gate(void) {
    stop_gate();
    reset_watchdog();
    _delay_ms(RELAY_SWITCHING_DELAY);
    PORTB |= (1 << RELAY_K1) | (1 << RELAY_K4);
    indicate_opening();
    gate_state = GATE_OPENING;
    report_state();
    gate_moving = 1;
    idle_timer_ms = 0;
    reset_scheduled = 0;

    for (uint16_t i = 0; i < GATE_OPERATION_TIME; i += SHORT_DELAY) {
        _delay_ms(SHORT_DELAY);
        reset_watchdog();
        if (!gate_moving) return;
    }

    stop_gate();
    gate_state = GATE_OPEN;
    write_gate_state(gate_state);
    report_state();
    uart_tx_string_P(PSTR("30 seconds have passed, setting gate to fully open\r\n"));
}

void close_gate(void) {
    stop_gate();
    reset_watchdog();
    _delay_ms(RELAY_SWITCHING_DELAY);
    PORTB |= (1 << RELAY_K2) | (1 << RELAY_K3);
    indicate_closing();
    gate_state = GATE_CLOSING;
    report_state();
    gate_moving = 1;
    idle_timer_ms = 0;
    reset_scheduled = 0;

    for (uint16_t i = 0; i < GATE_OPERATION_TIME; i += SHORT_DELAY) {
        _delay_ms(SHORT_DELAY);
        reset_watchdog();
        if (!gate_moving) return;
    }

    stop_gate();
    gate_state = GATE_CLOSED;
    write_gate_state(gate_state);
    report_state();
    uart_tx_string_P(PSTR("30 seconds have passed, setting gate to fully closed\r\n"));
}

void toggle_gate(void) {
    reset_watchdog();
    idle_timer_ms = 0;
    reset_scheduled = 0;
    
    if (gate_state == GATE_CLOSED || gate_state == GATE_CLOSING) {
        if (gate_state == GATE_CLOSING) {
            uart_tx_string_P(PSTR("Gate currently closing, changing direction to opening\r\n"));
        } else {
            uart_tx_string_P(PSTR("Gate currently closed, opening\r\n"));
        }
        open_gate();
    } else {
        if (gate_state == GATE_OPENING) {
            uart_tx_string_P(PSTR("Gate currently opening, changing direction to closing\r\n"));
        } else {
            uart_tx_string_P(PSTR("Gate currently open, closing\r\n"));
        }
        close_gate();
    }
}

void emergency_stop(void) {
    uart_tx_string_P(PSTR("Emergency stop: gate halted immediately\r\n"));
    stop_gate();
    reset_watchdog();
    idle_timer_ms = 0;
    reset_scheduled = 0;
    
    if (gate_state == GATE_OPENING) {
        gate_state = GATE_OPEN;
        uart_tx_string_P(PSTR("Gate movement interrupted while opening. Considering gate open\r\n"));
    } else if (gate_state == GATE_CLOSING) {
        gate_state = GATE_CLOSED;
        uart_tx_string_P(PSTR("Gate movement interrupted while closing. Considering gate closed\r\n"));
    }
    
    write_gate_state(gate_state);
    report_state();
}

ISR(INT0_vect) {
    _delay_ms(BUTTON_DEBOUNCE_DELAY); // Debounce
    reset_watchdog();
    
    if (!(PIND & (1 << BUTTON_PIN))) {
        while (!(PIND & (1 << BUTTON_PIN))) {
            _delay_ms(SHORT_DELAY);
            reset_watchdog();
        }

        idle_timer_ms = 0;
        reset_scheduled = 0;

        if (gate_moving) {
            emergency_stop();
            return;
        }

        button_pressed = 1;
    }
}

int main(void) {
    init_io();
    uart_init();

    uart_tx_string_P(PSTR("ATMega8535 booting\r\n"));
    uart_tx_string_P(PSTR("I/O initialized\r\n"));

    init_interrupts();
    uart_tx_string_P(PSTR("Interrupts enabled\r\n"));

    uint8_t mcucsr_value = MCUCSR;
    
    if (mcucsr_value & (1 << WDRF)) {
        uart_tx_string_P(PSTR("System restarted via watchdog reset\r\n"));
        MCUCSR &= ~(1 << WDRF);
    }
    
    if (mcucsr_value & (1 << PORF)) {
        uart_tx_string_P(PSTR("System experienced a power-on reset\r\n"));
        MCUCSR &= ~(1 << PORF);
    }
    
    if (mcucsr_value & (1 << EXTRF)) {
        uart_tx_string_P(PSTR("System experienced an external reset\r\n"));
        MCUCSR &= ~(1 << EXTRF);
    }
    
    if (mcucsr_value & (1 << BORF)) {
        uart_tx_string_P(PSTR("System experienced a brown-out reset\r\n"));
        MCUCSR &= ~(1 << BORF);
    }

    if (check_reset_flag()) {
        uart_tx_string_P(PSTR("System recovered from controlled reset\r\n"));
    }

    gate_state = read_gate_state();
    uart_tx_string_P(PSTR("EEPROM read complete\r\n"));

    init_watchdog();

    uart_tx_string_P(PSTR("ATMega8535 ready\r\n"));
    report_state();

    uint16_t ms_counter = 0;
    
    while (1) {
        reset_watchdog();
        
        if (button_pressed) {
            button_pressed = 0;
            toggle_gate();
        }
        
        _delay_ms(1);
        ms_counter++;
        
        if (!gate_moving) {
            idle_timer_ms++;
        }
        
        if (ms_counter >= 1000) {
            ms_counter = 0;
            
            if (idle_timer_ms >= (REGULAR_RESET_HOURS * MS_PER_HOUR) && !gate_moving && !reset_scheduled) {
                schedule_reset();
            }
            
            if (reset_scheduled && idle_timer_ms >= RESET_DELAY) {
                perform_controlled_reset();
            }
        }
    }
}
