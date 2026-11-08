// Negative test: unannotated extern function defaults to @cold and cannot be called from @nolatency
#include "veriobjc_runtime.h"

extern int unannotated_foreign_io(int fd);

veriobjc_nolatency
int hot_audio_tick(int fd) {
    return unannotated_foreign_io(fd);
}
