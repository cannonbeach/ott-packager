/* Copyright (c) Michael Anthony 2002 <mike at unlikely org>
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "fgetopt.h"
#include <string.h>
#include <stdio.h>

char *optarg = 0;
int optind = -1;
int opterr = 1;
int optopt = 0;
static int newinv = 1;

int fgetopt_long (
    int argc, char *const argv[],
    const char *optstring,
    const struct option *longopts,
    int *longindex)
{
    static int nextchar;

    // New invocation.
    if (newinv == 1)
    {
        optind = 0;
        nextchar = 0;
        newinv = 0;
    }

    // No arguments left.
    if (optind + 1 == argc)
    {
        ++optind;
        newinv = 1;
        return -1;
    }

    // New argument.
    if (nextchar == 0)
    {
        ++optind;

        // Long option or end-of-options marker.
        if (argv[optind][0] == '-'
                && argv[optind][1] == '-')
        {
            if (argv[optind][2] == '\0')
            {
                ++optind;
                return -1;
            }
            else
            {
                const char *begin = argv[optind] + 2;
                const char *end;
                const struct option *op;

                end = strchr (begin, '=');
                if (end == 0)
                {
                    end = begin + strlen (begin);
                }

                for (op = longopts; op->name; ++op)
                {
                    if (end - begin == strlen (op->name)
                            && strncmp (begin, op->name, end - begin) == 0)
                    {
                        int ret = 0;
                        if (! op->has_arg && *end == '=')
                        {
                            fprintf(stderr, "%s: option `--%s' doesn't"
                                    " allow an argument\n", argv[0], op->name);
                            ret = '?';
                        }
                        else
                        {
                            if (op->has_arg)
                            {
                                optarg = 0;
                                if (*end == '=')
                                {
                                    optarg = (char*) end + 1;
                                }
                                else if (optind + 1 >= argc
                                         && op->has_arg == required_argument)
                                {
                                    fprintf(stderr, "%s: option `--%s' requires"
                                            " an argument\n", argv[0], op->name);
                                    return '?';
                                }
                                else if (optind + 1 >= argc)
                                {
                                    // do nothing.
                                }
                                else if (argv[optind + 1][0] != '-'
                                         || op->has_arg == required_argument)
                                {
                                    optarg = argv[optind + 1];
                                    ++optind;
                                }
                            }
                            if (longindex)
                            {
                                *longindex = op - longopts;
                            }
                            if (op->flag)
                            {
                                *(((struct option*)op)->flag) = op->val;
                            }
                            else
                            {
                                ret = op->val;
                            }
                        }
                        return ret;
                    }
                }
                fprintf(stderr, "%s: unrecognized option `--%s'\n",
                        argv[0], begin);
                return '?';
            }
        }
        // Short option.
        else if (argv[optind][0] == '-' && argv[optind][1] != '\0')
        {
            nextchar = 1;
        }
        // Non-option.
        else
        {
            newinv = 1;
            return -1;
        }
    }

    // Second+ character of a short option.  Note that this cannot be an
    // else block because nextchar is updated in the block above.
    if (nextchar)
    {
        const char *op = strchr (optstring, argv[optind][nextchar]);
        ++nextchar;
        if (op)
        {
            if (op[1] == ':')
            {
                if (argv[optind][nextchar] != '\0')
                {
                    optarg = argv[optind] + nextchar;
                    nextchar = 0;
                }
                else if (optind + 1 >= argc)
                {
                    fprintf(stderr, "%s: option requires an argument -- %c\n",
                            argv[0], *op);
                    return '?';
                }
                else
                {
                    optarg = argv[optind + 1];
                    ++optind;
                    nextchar = 0;
                }
            }
            if (argv[optind][nextchar] == '\0')
            {
                nextchar = 0;
            }
            return *op;
        }
        else
        {
            fprintf(stderr, "%s: invalid option -- %c\n",
                    argv[0], argv[optind][nextchar - 1]);
            return '?';
        }
    }

    /* FIXME? */
    return -1;
}

