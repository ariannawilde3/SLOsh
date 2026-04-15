/**
 * SLOsh - San Luis Obispo Shell
 * CSC 453 - Operating Systems
 *
 * TODO: Complete the implementation according to the comments
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <unistd.h>
 #include <sys/wait.h>
 #include <sys/types.h>
 #include <fcntl.h>
 #include <signal.h>
 #include <limits.h>
 #include <errno.h>

/* Define PATH_MAX if it's not available */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define MAX_INPUT_SIZE 1024
#define MAX_ARGS 64
#define MAX_COMMANDS 32

typedef struct {
    char *args[MAX_ARGS];
    char *outfile;
    int append;
} Command;


/* Global variable for signal handling */
volatile sig_atomic_t child_running = 0;

/* Forward declarations */
void display_prompt(void);
static void reset_child_sigint_to_default(void);
static void apply_output_redirection_or_exit(Command *cmd);

/**
 * Signal handler for SIGINT (Ctrl+C)
 *
 * TODO: Handle Ctrl+C appropriately. Think about what behavior makes sense
 * when the user presses Ctrl+C - should the shell exit? should a child process
 * be interrupted?
 * Hint: The global variable tracks important state.
 */
void sigint_handler(int sig) {

    (void)sig;
    if (!child_running) {
        write(STDOUT_FILENO, "\n", 1);
        display_prompt();
    }
}

/**
 * Display the command prompt with current directory
 */
void display_prompt(void) {
    char cwd[PATH_MAX];
    char prompt_buf[PATH_MAX + 3];
    int len;

    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        len = snprintf(prompt_buf, sizeof(prompt_buf), "%s> ", cwd);
    } else {
        len = snprintf(prompt_buf, sizeof(prompt_buf), "SLOsh> ");
    }

    if (len > 0 && len < (int)sizeof(prompt_buf)) {
        write(STDOUT_FILENO, prompt_buf, len);
    }
}

/**
 * Parse the input line into command arguments
 *
 * TODO: Extract tokens from the input string. What should you do with special
 * characters like pipes and redirections? How will the rest of the code know
 * what to execute?
 * Hint: You'll need to handle more than just splitting on spaces.
 *
 * @param input The input string to parse
 * @param args Array to store parsed arguments
 * @return Number of arguments parsed
 */
int parse_input(char *input, Command *commands, int max_commands) {
    int cmd_idx = 0;
    int arg_idx = 0;
    char *token;
    char *saveptr = NULL;

    if (input == NULL || commands == NULL || max_commands <= 0) {
        return 0;
    }

    for (int i = 0; i < max_commands; i++) {
        commands[i].outfile = NULL;
        commands[i].append = 0;
        for (int j = 0; j < MAX_ARGS; j++) {
            commands[i].args[j] = NULL;
        }
    }

    input[strcspn(input, "\n")] = '\0';
    token = strtok_r(input, " \t", &saveptr);

    while (token != NULL) {
        if (strcmp(token, "|") == 0) {
            if (arg_idx == 0) {
                return 0;
            }
            commands[cmd_idx].args[arg_idx] = NULL;
            cmd_idx++;
            if (cmd_idx >= max_commands) {
                return max_commands;
            }
            arg_idx = 0;
        } else if (strcmp(token, ">") == 0 || strcmp(token, ">>") == 0) {
            commands[cmd_idx].append = (strcmp(token, ">>") == 0);
            token = strtok_r(NULL, " \t", &saveptr);
            if (token == NULL) {
                return 0;
            }
            commands[cmd_idx].outfile = token;
        } else {
            if (arg_idx < MAX_ARGS - 1) {
                commands[cmd_idx].args[arg_idx++] = token;
            }
        }

        token = strtok_r(NULL, " \t", &saveptr);
    }

    if (cmd_idx == 0 && arg_idx == 0) {
        return 0;
    }

    commands[cmd_idx].args[arg_idx] = NULL;
    return cmd_idx + 1;
}

// part 7: status reporting 

void report_status(int status) {
    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code != 0) {
            fprintf(stderr, "exit status %d\n", code);
        }
    } else if (WIFSIGNALED(status)) {
        fprintf(stderr, "terminated by signal %d\n", WTERMSIG(status));
    } else {
        printf("unknown status\n");
    }
}

static void reset_child_sigint_to_default(void) {
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
}

static void apply_output_redirection_or_exit(Command *cmd) {
    int fd;

    if (cmd->outfile == NULL) {
        return;
    }

    if (cmd->append) {
        fd = open(cmd->outfile, O_WRONLY | O_CREAT | O_APPEND, 0644);
    } else {
        fd = open(cmd->outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    }

    if (fd < 0) {
        perror("open");
        exit(1);
    }

    dup2(fd, STDOUT_FILENO);
    close(fd);
}

/**
 * Execute the given command with its arguments
 *
 * TODO: Run the command. Your implementation should handle:
 * - Basic command execution
 * - Pipes (|)
 * - Output redirection (> and >>)
 *
 * What system calls will you need? How do you connect processes together?
 * How do you redirect file descriptors?
 *
 * @param args Array of command arguments (NULL-terminated)
 */
void execute_command(Command *commands, int numberOfCommands) {

    if (numberOfCommands == 1){

        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            return;
        }

        if (pid == 0){

            reset_child_sigint_to_default();

            // part 5
            apply_output_redirection_or_exit(&commands[0]);

            execvp(commands[0].args[0], commands[0].args);
            perror("slosh");
            exit(1);

        } else {
            child_running = 1;
            int wstatus;
            if (waitpid(pid, &wstatus, 0) > 0) {
                report_status(wstatus);
            }
            child_running = 0;
        }
    } else {
        // part 6 (pipes)
        int pipes[MAX_COMMANDS][2];
        pid_t pids[MAX_COMMANDS];

        child_running = 1;

        // create a pipe for each command 
        for (int i = 0; i < numberOfCommands - 1; i++) {
            if (pipe(pipes[i]) == -1) {
                perror("pipe");
                return;
            }
        }

        // fork a child for each command
        for (int i = 0; i < numberOfCommands; i++) {
            pid_t pid = fork();

            if (pid < 0) {
                perror("fork");
                return;
            }

            if (pid == 0) {
                reset_child_sigint_to_default();

                // redirect input from previous pipe
                if (i > 0) {
                    dup2(pipes[i - 1][0], STDIN_FILENO);
                }

                // redirect output to next pipe
                if (i < numberOfCommands - 1) {
                    dup2(pipes[i][1], STDOUT_FILENO);
                }

                 if (i == numberOfCommands - 1) {
                     apply_output_redirection_or_exit(&commands[i]);
                 }

                // close all pipe fds in child
                for (int j = 0; j < numberOfCommands - 1; j++) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }

                execvp(commands[i].args[0], commands[i].args);
                perror("slosh");
                exit(1);
            }

            pids[i] = pid;
        }

        // close all pipe fds in parent
        for (int i = 0; i < numberOfCommands - 1; i++) {
            close(pipes[i][0]);
            close(pipes[i][1]); 
        }

        // wait for all children to finish
        for (int i = 0; i < numberOfCommands; i++) {
            int wstatus;
            if (waitpid(pids[i], &wstatus, 0) > 0) {
                report_status(wstatus);
            }
        }
        child_running = 0;
    }
}

/**
 * Check for and handle built-in commands
 *
 * TODO: Implement support for built-in commands:
 * - exit: Exit the shell
 * - cd: Change directory
 *
 * @param args Array of command arguments (NULL-terminated)
 * @return 0 to exit shell, 1 to continue, -1 if not a built-in command
 */
int handle_builtin(char **args) {
    
    if (strcmp (args[0], "exit") == 0) {
        return 0;
    }

    if (strcmp(args[0], "cd") == 0) {

        if (args[1] == NULL) {
            fprintf(stderr, "missing an argument\n");
        } else if (chdir(args[1]) != 0) {
            perror("cd");
        }

        return 1;
    }

    return -1;
}

int main(void) {

    /* part 4 setting up the signal */
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction (SIGINT, &sa, NULL);


    char input[MAX_INPUT_SIZE];
    Command commands [MAX_COMMANDS];
    int numberOfCommands;
    int status = 1;
    int builtin_result;

    while (status) {
        display_prompt();

        /* Read input and handle signal interruption */
        if (fgets(input, MAX_INPUT_SIZE, stdin) == NULL) {
            if (!feof(stdin)) {
                perror("fgets");
            }
            break;
        }

        /* Parse input */
        numberOfCommands = parse_input(input, commands, MAX_COMMANDS);

        /* Handle empty command */
        if (numberOfCommands <= 0 || commands[0].args[0] == NULL) {
            continue;
        }

        /* Check for built-in commands */
        builtin_result = handle_builtin(commands[0].args);
        if (builtin_result >= 0) {
            status = builtin_result;
            continue;
        }

        /* Execute external command */
        execute_command(commands, numberOfCommands);
    }

    printf("SLOsh exiting...\n");
    return EXIT_SUCCESS;
}
