#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>

#define MAX_LINE 80 /* The maximum length command */
#define DELIMITERS " \t\n\v\f\r"

void init_comd(char *comd) {
    strcpy(comd, "");
}

void init_args(char *args[]) {
    for(size_t i = 0; i != MAX_LINE / 2 + 1; ++i) {
        args[i] = NULL;
    }
}

//free up old content and set back to NULL
void refresh_args(char *args[]) {
    while(*args) {
        free(*args);  
        *args++ = NULL;
    }
}

size_t parse_input(char *args[], char *org_comd) {
    size_t n = 0;
    char comd[MAX_LINE + 1];
    strcpy(comd, org_comd);
    char *tk = strtok(comd, DELIMITERS);
    while(tk != NULL) {
        args[n] = malloc(strlen(tk) + 1);
        strcpy(args[n], tk);
        ++n;
        tk = strtok(NULL, DELIMITERS);
    }
    return n;
}

int get_input(char *comd) {
    char buffer[MAX_LINE + 1];
    if(fgets(buffer, MAX_LINE + 1, stdin) == NULL) {
        fprintf(stderr, "Failed to read input.\n");
        return 0;
    }
    if(strncmp(buffer, "!!", 2) == 0) {
        if(strlen(comd) == 0) {
            fprintf(stderr, "There's no history.\n");
            return 0;
        }
        printf("%s", comd); 
        return 1;
    }
    strcpy(comd, buffer);
    return 1;
}

int check_ampersand(char **args, size_t *size) {
    size_t len = strlen(args[*size - 1]);
    if(args[*size - 1][len - 1] != '&') {
        return 0;
    }
    if(len == 1) {
        free(args[*size - 1]);
        args[*size - 1] = NULL;
        --(*size);
    } else {
        args[*size - 1][len - 1] = '\0';
    }
    return 1;
}

unsigned ck_redirect(char **args, size_t *size, char **inpt_f, char **outpt_f) {
    unsigned flag = 0;
    size_t to_remove[4], remove_cnt = 0;
    for(size_t i = 0; i != *size; ++i) {
        if(remove_cnt >= 4) {
            break;
        }
        if(strcmp("<", args[i]) == 0) {
            to_remove[remove_cnt++] = i;
            if(i == (*size) - 1) {
                fprintf(stderr, "No input file.\n");
                break;
            }
            flag |= 1;
            *inpt_f = args[i + 1];
            to_remove[remove_cnt++] = ++i;
        } else if(strcmp(">", args[i]) == 0) {
            to_remove[remove_cnt++] = i;
            if(i == (*size) - 1) {
                fprintf(stderr, "No output file.\n");
                break;
            }
            flag |= 2;
            *outpt_f = args[i + 1];
            to_remove[remove_cnt++] = ++i;
        }
    }
    for(int i = remove_cnt - 1; i >= 0; --i) {
        size_t pos = to_remove[i];
        while(pos != *size) {
            args[pos] = args[pos + 1];
            ++pos;
        }
        --(*size);
    }
    return flag;
}

int redirect(unsigned io_flag, char *inpt_f, char *outpt_f, int *input_desc, int *output_desc) {
    if(io_flag & 2) {
        *output_desc = open(outpt_f, O_WRONLY | O_CREAT | O_TRUNC, 644);
        if(*output_desc < 0) {
            fprintf(stderr, "Failed to open the output file: %s\n", outpt_f);
            return 0;
        }
        dup2(*output_desc, STDOUT_FILENO);
    }
    if(io_flag & 1) {
        *input_desc = open(inpt_f, O_RDONLY, 0644);
        if(*input_desc < 0) {
            fprintf(stderr, "Failed to open the input file: %s\n", inpt_f);
            return 0;
        }
        dup2(*input_desc, STDIN_FILENO);
    }
    return 1;
}

void close_f(unsigned io_flag, int input_desc, int output_desc) {
    if(io_flag & 2) {
        close(output_desc);
    }
    if(io_flag & 1) {
        close(input_desc);
    }
}

void detector(char **args, size_t *args_num, char ***args2, size_t *args_num2) {
    for(size_t i = 0; i != *args_num; ++i) {
        if (strcmp(args[i], "|") == 0) {
            free(args[i]);
            args[i] = NULL;
            *args_num2 = *args_num -  i - 1;
            *args_num = i;
            *args2 = args + i + 1;
            break;
        }
    }
}

int run_comd(char **args, size_t args_num) {
    int run_concurrently = check_ampersand(args, &args_num);
    char **args2;
    size_t args_num2 = 0;
    detector(args, &args_num, &args2, &args_num2);
    pid_t pid = fork();
    if(pid < 0) {
        fprintf(stderr, "Unable to fork.\n");
        return 0;
    } else if (pid == 0) {
        if(args_num2 != 0) {
            int fd[2];
            pipe(fd);
            pid_t pid2 = fork();
            if(pid2 > 0) {
                char *inpt_f, *outpt_f;
                int input_desc, output_desc;
                unsigned io_flag = ck_redirect(args2, &args_num2, &inpt_f, &outpt_f);
                io_flag &= 2;
                if(redirect(io_flag, inpt_f, outpt_f, &input_desc, &output_desc) == 0) {
                    return 0;
                }
                close(fd[1]);
                dup2(fd[0], STDIN_FILENO);
                wait(NULL);
                execvp(args2[0], args2);
                close_f(io_flag, input_desc, output_desc);
                close(fd[0]);
                fflush(stdin);
            } else if(pid2 == 0) {
                char *inpt_f, *outpt_f;
                int input_desc, output_desc;
                unsigned io_flag = ck_redirect(args, &args_num, &inpt_f, &outpt_f);
                io_flag &= 1;
                if(redirect(io_flag, inpt_f, outpt_f, &input_desc, &output_desc) == 0) {
                    return 0;
                }
                close(fd[0]);
                dup2(fd[1], STDOUT_FILENO);
                execvp(args[0], args);
                close_f(io_flag, input_desc, output_desc);
                close(fd[1]);
                fflush(stdin);
            }
        } else {
            char *inpt_f, *outpt_f;
            int input_desc, output_desc;
            unsigned io_flag = ck_redirect(args, &args_num, &inpt_f, &outpt_f);
            if(redirect(io_flag, inpt_f, outpt_f, &input_desc, &output_desc) == 0) {
                return 0;
            }
            execvp(args[0], args);
            close_f(io_flag, input_desc, output_desc);
            fflush(stdin);
        }
    } else {
        if(!run_concurrently) {
            wait(NULL);
        }
    }
    return 1;
}

int main(void) {
    char *args[MAX_LINE / 2 + 1]; /* command line arguments */
    char comd[MAX_LINE + 1];
    int should_run = 1;

    init_args(args);
    init_comd(comd);
    
    while (should_run) {
        printf("osh>");
        fflush(stdout);

        fflush(stdin);
        refresh_args(args);
        if(!get_input(comd)) {
            continue;
        }
        size_t args_num = parse_input(args, comd);
        if(args_num == 0) {
            printf("Enter command or type \"exit\" to quit\n");
            continue;
        }
        if(strcmp(args[0], "exit") == 0) {
            break;
        }
        run_comd(args, args_num);
    }
    refresh_args(args);
    return 0;
}