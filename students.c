/* students.c — load student registry from CSV and validate attendance status */
#define _POSIX_C_SOURCE 200809L

#include "attendance.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static void trim_newline(char *line)
{
    size_t len = strlen(line);

    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[len - 1] = '\0';
        len--;
    }
}

static bool parse_student_line(const char *line, Student *student)
{
    char buffer[256];
    char *token;
    char *saveptr = NULL;
    int field = 0;

    if (line == NULL || student == NULL) {
        return false;
    }

    strncpy(buffer, line, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    token = strtok_r(buffer, ",", &saveptr);
    while (token != NULL && field < 3) {
        while (*token != '\0' && isspace((unsigned char)*token)) {
            token++;
        }

        switch (field) {
        case 0:
            strncpy(student->student_id, token, sizeof(student->student_id) - 1);
            student->student_id[sizeof(student->student_id) - 1] = '\0';
            break;
        case 1:
            strncpy(student->full_name, token, sizeof(student->full_name) - 1);
            student->full_name[sizeof(student->full_name) - 1] = '\0';
            break;
        case 2:
            strncpy(student->course_code, token, sizeof(student->course_code) - 1);
            student->course_code[sizeof(student->course_code) - 1] = '\0';
            break;
        default:
            break;
        }

        field++;
        token = strtok_r(NULL, ",", &saveptr);
    }

    return field == 3 && student->student_id[0] != '\0';
}

/* Load students from CSV: id,name,course (lines starting with # are ignored) */
bool load_student_registry(StudentRegistry *registry, const char *path)
{
    FILE *file;
    char line[256];
    size_t loaded = 0;

    if (registry == NULL || path == NULL) {
        return false;
    }

    memset(registry, 0, sizeof(*registry));

    file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "ERROR: Could not open %s. Ensure the student registry file exists.\n", path);
        return false;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        Student student;

        trim_newline(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        if (!parse_student_line(line, &student)) {
            fprintf(stderr, "WARNING: Skipping malformed student line: %s\n", line);
            continue;
        }

        if (loaded >= MAX_STUDENTS) {
            fprintf(stderr, "ERROR: Student registry exceeds capacity (%d).\n", MAX_STUDENTS);
            fclose(file);
            return false;
        }

        registry->students[loaded++] = student;
    }

    fclose(file);
    registry->count = loaded;

    if (registry->count == 0) {
        fprintf(stderr, "ERROR: %s is empty. Add at least one student record.\n", path);
        return false;
    }

    printf("Loaded %zu student(s) from %s.\n", registry->count, path);
    return true;
}

const Student *find_student(const StudentRegistry *registry, const char *student_id)
{
    size_t i;

    if (registry == NULL || student_id == NULL) {
        return NULL;
    }

    for (i = 0; i < registry->count; i++) {
        if (strcmp(registry->students[i].student_id, student_id) == 0) {
            return &registry->students[i];
        }
    }

    return NULL;
}

bool is_valid_status(const char *status)
{
    if (status == NULL) {
        return false;
    }

    return strcmp(status, "PRESENT") == 0 || strcmp(status, "ABSENT") == 0 ||
           strcmp(status, "LATE") == 0;
}
