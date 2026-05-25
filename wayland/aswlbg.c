#include "aswlbg_internal.h"

int main(void)
{
	struct as_state state = {
		.width = 1024,
		.height = 768,
		.running = true,
	};

	return aswlbg_run(&state);
}
