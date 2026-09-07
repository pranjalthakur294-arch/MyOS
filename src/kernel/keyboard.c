#include "keyboard.h"
#include "io.h"
#include "pic.h"
#include "terminal.h"

/* Modifier states */
static int left_shift = 0;
static int right_shift = 0;
static int caps_lock = 0;

/*
 * PS/2 Scancode Set 1 standard translation tables.
 * Index represents the make scancode (0x00 - 0x58).
 */
static const char kbd_us_nomod[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   /* Left Control */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   /* Left Shift */
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    0,   /* Right Shift */
    '*',
    0,   /* Left Alt */
    ' ', /* Space */
    0,   /* Caps Lock */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* F1 - F10 */
    0,   /* Num Lock */
    0,   /* Scroll Lock */
    0,   /* Home */
    0,   /* Up Arrow */
    0,   /* Page Up */
    '-',
    0,   /* Left Arrow */
    0,
    0,   /* Right Arrow */
    '+',
    0,   /* End */
    0,   /* Down Arrow */
    0,   /* Page Down */
    0,   /* Insert */
    0    /* Delete */
};

static const char kbd_us_shift[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   /* Left Control */
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   /* Left Shift */
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0,   /* Right Shift */
    '*',
    0,   /* Left Alt */
    ' ', /* Space */
    0,   /* Caps Lock */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* F1 - F10 */
    0,   /* Num Lock */
    0,   /* Scroll Lock */
    0,   /* Home */
    0,   /* Up Arrow */
    0,   /* Page Up */
    '-',
    0,   /* Left Arrow */
    0,
    0,   /* Right Arrow */
    '+',
    0,   /* End */
    0,   /* Down Arrow */
    0,   /* Page Down */
    0,   /* Insert */
    0    /* Delete */
};

/*
 * keyboard_init - Flushes existing buffer, resets modifiers, and unmasks IRQ1.
 */
void keyboard_init(void) {
    /* Flush any pending bytes in the 8042 controller output buffer */
    while (inb(KBD_STATUS_PORT) & 1) {
        inb(KBD_DATA_PORT);
    }

    left_shift = 0;
    right_shift = 0;
    caps_lock = 0;

    /* Unmask IRQ1 on the Master PIC so keyboard interrupts reach the CPU */
    pic_clear_mask(1);
}

/*
 * keyboard_handler - C handler called by isr_keyboard on IRQ1 (vector 0x21).
 */
void keyboard_handler(void) {
    /* 1. Read hardware scancode from port 0x60 */
    uint8_t scancode = inb(KBD_DATA_PORT);

    /* 2. Send End of Interrupt (EOI) for IRQ1 to PIC */
    pic_send_eoi(1);

    /* 3. Check for key release (Break code, bit 7 set) */
    if (scancode & 0x80) {
        uint8_t released_key = scancode & 0x7F;
        if (released_key == 0x2A) {
            left_shift = 0;
        } else if (released_key == 0x36) {
            right_shift = 0;
        }
        return;
    }

    /* 4. Handle key press (Make code, bit 7 clear) */
    if (scancode == 0x2A) {
        left_shift = 1;
        return;
    }
    if (scancode == 0x36) {
        right_shift = 1;
        return;
    }
    if (scancode == 0x3A) {
        caps_lock = !caps_lock;
        return;
    }

    /* 5. Translate scancode to ASCII */
    if (scancode < 128) {
        int shift_active = (left_shift || right_shift);

        char c;
        if (shift_active) {
            c = kbd_us_shift[scancode];
            /* If CapsLock is active on a letter, revert to lowercase */
            if (caps_lock && (c >= 'A' && c <= 'Z')) {
                c = (char)(c + ('a' - 'A'));
            }
        } else {
            c = kbd_us_nomod[scancode];
            /* If CapsLock is active on a letter, convert to uppercase */
            if (caps_lock && (c >= 'a' && c <= 'z')) {
                c = (char)(c - ('a' - 'A'));
            }
        }

        /* 6. Forward character to terminal discipline layer */
        if (c != 0) {
            terminal_putc(c);
        }
    }
}
