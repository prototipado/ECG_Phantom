#ifndef TERMINAL_CONTROL_H
#define TERMINAL_CONTROL_H

/**
 * @brief Initializes terminal control.
 */
void terminal_control_init(void);

/**
 * @brief Polls for terminal input and mirrors the menu if needed.
 * Should be called periodically from the main loop.
 */
void terminal_control_update(void);

#endif // TERMINAL_CONTROL_H
