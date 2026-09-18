#ifndef CLI_H
#define CLI_H

#include <stdbool.h>

void cli_init(void);
void cli_feed_char(int c);
bool cli_is_collecting(void);
void cli_start_collecting(void);
bool cli_is_streaming(void);
void cli_set_streaming(bool on);
bool cli_output_active(void);

#endif // CLI_H
