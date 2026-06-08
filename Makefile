CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -O2
LDFLAGS = -lssl -lcrypto

TARGET = attendance
SRCS = main.c students.c blockchain.c crypto.c pending.c transactions.c ledger.c mining.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean run test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c attendance.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET) attendance_chain.dat attendance_private.pem attendance_public.pem

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	@chmod +x tests/run_tests.sh
	@./tests/run_tests.sh
