CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic
OBJECTS = team_shell.o text_commands.o directory_commands.o file_commands.o

team_shell: $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@

$(OBJECTS): commands.h

.PHONY: clean
clean:
	$(RM) $(OBJECTS) team_shell
