/* Copyright (C) Michael Anthony 2002 <mike at unlikely org>
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:

 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef FGETOPT_H
#define FGETOPT_H

#if defined(__cplusplus)
extern "C" {
#endif

extern char *optarg;
extern int optind, opterr, optopt;

#ifdef no_argument
#undef no_argument
#endif
#define no_argument 0
#ifdef required_argument
#undef required_argument
#endif
#define required_argument 1
#ifdef optional_argument
#undef optional_argument
#endif
#define optional_argument 2

struct option
{
    const char *name;
    int has_arg;
    int *flag;
    int val;
};

int fgetopt_long (int argc, char *const argv[],
                  const char *optstring,
                  const struct option *longopts, int *longindex);

#if defined(__cplusplus)
}
#endif

#endif
