/*
 * Probe template.
 *
 * A probe is the smallest subject that answers the question: a few dozen lines
 * that exercise exactly one API or behaviour, so that the observation on macOS
 * is unambiguous.  Replace probe_main() with the call under investigation and
 * keep everything else -- the output format matters, because the JSON it
 * prints is the evidence.
 *
 * Build:  make            (macOS, links a framework named by FRAMEWORK=)
 * Run:    ./probe | tee ../evidence/probe-run-macos.json
 *
 * Output contract: one JSON object per line, each with "step" and either
 * "result" or "error", so runs on different systems can be diffed directly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#endif

static void emit(const char *step, const char *result, long value, int err)
{
    printf("{\"step\": \"%s\"", step);
    if (result) {
        printf(", \"result\": \"%s\"", result);
    }
    if (value != -1) {
        printf(", \"value\": %ld", value);
    }
    if (err != 0) {
        printf(", \"error\": %d, \"strerror\": \"%s\"", err, strerror(err));
    }
    printf("}\n");
    fflush(stdout);
}

int main(void)
{
    emit("start", "probe", -1, 0);

#if defined(__APPLE__)
    /* Example: what does the current task's bootstrap port look like? */
    mach_port_t bootstrap = MACH_PORT_NULL;
    kern_return_t kr = task_get_bootstrap_port(mach_task_self(), &bootstrap);
    emit("task_get_bootstrap_port", kr == KERN_SUCCESS ? "ok" : "fail",
         (long)bootstrap, kr == KERN_SUCCESS ? 0 : (int)kr);
#else
    emit("task_get_bootstrap_port", "unsupported-host", -1, 0);
#endif

    /* TODO: the actual call under investigation. */
    emit("done", "probe", -1, 0);
    return 0;
}
