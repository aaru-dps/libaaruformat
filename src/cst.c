/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2022 Natalia Portillo.
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <aaruformat.h>

int32_t aaruf_cst_transform(const uint8_t* interleaved, uint8_t* sequential, size_t length)
{
    uint8_t *p, *q, *r, *s, *t, *u, *v, *w;
    size_t   qStart;
    size_t   rStart;
    size_t   sStart;
    size_t   tStart;
    size_t   uStart;
    size_t   vStart;
    size_t   wStart;
    size_t   i;

    if(interleaved == NULL || sequential == NULL) return AARUF_ERROR_BUFFER_TOO_SMALL;

    p = malloc(length / 8);
    q = malloc(length / 8);
    r = malloc(length / 8);
    s = malloc(length / 8);
    t = malloc(length / 8);
    u = malloc(length / 8);
    v = malloc(length / 8);
    w = malloc(length / 8);

    if(p == NULL || q == NULL || r == NULL || s == NULL || t == NULL || u == NULL || v == NULL || w == NULL)
    {
        free(p);
        free(q);
        free(r);
        free(s);
        free(t);
        free(u);
        free(v);
        free(w);
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    for(i = 0; i < length; i += 8)
    {
        p[i / 8] = (uint8_t)(interleaved[i] & 0x80);
        p[i / 8] += (interleaved[i + 1] & 0x80) >> 1;
        p[i / 8] += (interleaved[i + 2] & 0x80) >> 2;
        p[i / 8] += (interleaved[i + 3] & 0x80) >> 3;
        p[i / 8] += (interleaved[i + 4] & 0x80) >> 4;
        p[i / 8] += (interleaved[i + 5] & 0x80) >> 5;
        p[i / 8] += (interleaved[i + 6] & 0x80) >> 6;
        p[i / 8] += (interleaved[i + 7] & 0x80) >> 7;

        q[i / 8] = (uint8_t)((interleaved[i] & 0x40) << 1);
        q[i / 8] += interleaved[i + 1] & 0x40;
        q[i / 8] += (interleaved[i + 2] & 0x40) >> 1;
        q[i / 8] += (interleaved[i + 3] & 0x40) >> 2;
        q[i / 8] += (interleaved[i + 4] & 0x40) >> 3;
        q[i / 8] += (interleaved[i + 5] & 0x40) >> 4;
        q[i / 8] += (interleaved[i + 6] & 0x40) >> 5;
        q[i / 8] += (interleaved[i + 7] & 0x40) >> 6;

        r[i / 8] = (uint8_t)((interleaved[i] & 0x20) << 2);
        r[i / 8] += (interleaved[i + 1] & 0x20) << 1;
        r[i / 8] += interleaved[i + 2] & 0x20;
        r[i / 8] += (interleaved[i + 3] & 0x20) >> 1;
        r[i / 8] += (interleaved[i + 4] & 0x20) >> 2;
        r[i / 8] += (interleaved[i + 5] & 0x20) >> 3;
        r[i / 8] += (interleaved[i + 6] & 0x20) >> 4;
        r[i / 8] += (interleaved[i + 7] & 0x20) >> 5;

        s[i / 8] = (uint8_t)((interleaved[i] & 0x10) << 3);
        s[i / 8] += (interleaved[i + 1] & 0x10) << 2;
        s[i / 8] += (interleaved[i + 2] & 0x10) << 1;
        s[i / 8] += interleaved[i + 3] & 0x10;
        s[i / 8] += (interleaved[i + 4] & 0x10) >> 1;
        s[i / 8] += (interleaved[i + 5] & 0x10) >> 2;
        s[i / 8] += (interleaved[i + 6] & 0x10) >> 3;
        s[i / 8] += (interleaved[i + 7] & 0x10) >> 4;

        t[i / 8] = (uint8_t)((interleaved[i] & 0x08) << 4);
        t[i / 8] += (interleaved[i + 1] & 0x08) << 3;
        t[i / 8] += (interleaved[i + 2] & 0x08) << 2;
        t[i / 8] += (interleaved[i + 3] & 0x08) << 1;
        t[i / 8] += interleaved[i + 4] & 0x08;
        t[i / 8] += (interleaved[i + 5] & 0x08) >> 1;
        t[i / 8] += (interleaved[i + 6] & 0x08) >> 2;
        t[i / 8] += (interleaved[i + 7] & 0x08) >> 3;

        u[i / 8] = (uint8_t)((interleaved[i] & 0x04) << 5);
        u[i / 8] += (interleaved[i + 1] & 0x04) << 4;
        u[i / 8] += (interleaved[i + 2] & 0x04) << 3;
        u[i / 8] += (interleaved[i + 3] & 0x04) << 2;
        u[i / 8] += (interleaved[i + 4] & 0x04) << 1;
        u[i / 8] += interleaved[i + 5] & 0x04;
        u[i / 8] += (interleaved[i + 6] & 0x04) >> 1;
        u[i / 8] += (interleaved[i + 7] & 0x04) >> 2;

        v[i / 8] = (uint8_t)((interleaved[i] & 0x02) << 6);
        v[i / 8] += (interleaved[i + 1] & 0x02) << 5;
        v[i / 8] += (interleaved[i + 2] & 0x02) << 4;
        v[i / 8] += (interleaved[i + 3] & 0x02) << 3;
        v[i / 8] += (interleaved[i + 4] & 0x02) << 2;
        v[i / 8] += (interleaved[i + 5] & 0x02) << 1;
        v[i / 8] += interleaved[i + 6] & 0x02;
        v[i / 8] += (interleaved[i + 7] & 0x02) >> 1;

        w[i / 8] = (uint8_t)((interleaved[i] & 0x01) << 7);
        w[i / 8] += (interleaved[i + 1] & 0x01) << 6;
        w[i / 8] += (interleaved[i + 2] & 0x01) << 5;
        w[i / 8] += (interleaved[i + 3] & 0x01) << 4;
        w[i / 8] += (interleaved[i + 4] & 0x01) << 3;
        w[i / 8] += (interleaved[i + 5] & 0x01) << 2;
        w[i / 8] += (interleaved[i + 6] & 0x01) << 1;
        w[i / 8] += interleaved[i + 7] & 0x01;
    }

    qStart = (length / 8) * 1;
    rStart = (length / 8) * 2;
    sStart = (length / 8) * 3;
    tStart = (length / 8) * 4;
    uStart = (length / 8) * 5;
    vStart = (length / 8) * 6;
    wStart = (length / 8) * 7;

    for(i = 0; i < (length / 8); i++)
    {
        sequential[i]          = p[i];
        sequential[qStart + i] = q[i];
        sequential[rStart + i] = r[i];
        sequential[sStart + i] = s[i];
        sequential[tStart + i] = t[i];
        sequential[uStart + i] = u[i];
        sequential[vStart + i] = v[i];
        sequential[wStart + i] = w[i];
    }

    return AARUF_STATUS_OK;
}

int32_t aaruf_cst_untransform(const uint8_t* sequential, uint8_t* interleaved, size_t length)
{
    uint8_t *p, *q, *r, *s, *t, *u, *v, *w;
    size_t   qStart;
    size_t   rStart;
    size_t   sStart;
    size_t   tStart;
    size_t   uStart;
    size_t   vStart;
    size_t   wStart;
    size_t   i;

    if(interleaved == NULL || sequential == NULL) return AARUF_ERROR_BUFFER_TOO_SMALL;

    p = malloc(length / 8);
    q = malloc(length / 8);
    r = malloc(length / 8);
    s = malloc(length / 8);
    t = malloc(length / 8);
    u = malloc(length / 8);
    v = malloc(length / 8);
    w = malloc(length / 8);

    if(p == NULL || q == NULL || r == NULL || s == NULL || t == NULL || u == NULL || v == NULL || w == NULL)
    {
        free(p);
        free(q);
        free(r);
        free(s);
        free(t);
        free(u);
        free(v);
        free(w);
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    qStart = (length / 8) * 1;
    rStart = (length / 8) * 2;
    sStart = (length / 8) * 3;
    tStart = (length / 8) * 4;
    uStart = (length / 8) * 5;
    vStart = (length / 8) * 6;
    wStart = (length / 8) * 7;

    for(i = 0; i < (length / 8); i++)
    {
        p[i] = sequential[i];
        q[i] = sequential[qStart + i];
        r[i] = sequential[rStart + i];
        s[i] = sequential[sStart + i];
        t[i] = sequential[tStart + i];
        u[i] = sequential[uStart + i];
        v[i] = sequential[vStart + i];
        w[i] = sequential[wStart + i];
    }

    memset(interleaved, 0, length);

    for(i = 0; i < length; i += 8)
    {
        interleaved[i] += ((p[i / 8] & 0x80) == 0x80 ? 0x80 : 0);
        interleaved[i + 1] += ((p[i / 8] & 0x40) == 0x40 ? 0x80 : 0);
        interleaved[i + 2] += ((p[i / 8] & 0x20) == 0x20 ? 0x80 : 0);
        interleaved[i + 3] += ((p[i / 8] & 0x10) == 0x10 ? 0x80 : 0);
        interleaved[i + 4] += ((p[i / 8] & 0x08) == 0x08 ? 0x80 : 0);
        interleaved[i + 5] += ((p[i / 8] & 0x04) == 0x04 ? 0x80 : 0);
        interleaved[i + 6] += ((p[i / 8] & 0x02) == 0x02 ? 0x80 : 0);
        interleaved[i + 7] += ((p[i / 8] & 0x01) == 0x01 ? 0x80 : 0);

        interleaved[i] += ((q[i / 8] & 0x80) == 0x80 ? 0x40 : 0);
        interleaved[i + 1] += ((q[i / 8] & 0x40) == 0x40 ? 0x40 : 0);
        interleaved[i + 2] += ((q[i / 8] & 0x20) == 0x20 ? 0x40 : 0);
        interleaved[i + 3] += ((q[i / 8] & 0x10) == 0x10 ? 0x40 : 0);
        interleaved[i + 4] += ((q[i / 8] & 0x08) == 0x08 ? 0x40 : 0);
        interleaved[i + 5] += ((q[i / 8] & 0x04) == 0x04 ? 0x40 : 0);
        interleaved[i + 6] += ((q[i / 8] & 0x02) == 0x02 ? 0x40 : 0);
        interleaved[i + 7] += ((q[i / 8] & 0x01) == 0x01 ? 0x40 : 0);

        interleaved[i] += ((r[i / 8] & 0x80) == 0x80 ? 0x20 : 0);
        interleaved[i + 1] += ((r[i / 8] & 0x40) == 0x40 ? 0x20 : 0);
        interleaved[i + 2] += ((r[i / 8] & 0x20) == 0x20 ? 0x20 : 0);
        interleaved[i + 3] += ((r[i / 8] & 0x10) == 0x10 ? 0x20 : 0);
        interleaved[i + 4] += ((r[i / 8] & 0x08) == 0x08 ? 0x20 : 0);
        interleaved[i + 5] += ((r[i / 8] & 0x04) == 0x04 ? 0x20 : 0);
        interleaved[i + 6] += ((r[i / 8] & 0x02) == 0x02 ? 0x20 : 0);
        interleaved[i + 7] += ((r[i / 8] & 0x01) == 0x01 ? 0x20 : 0);

        interleaved[i] += ((s[i / 8] & 0x80) == 0x80 ? 0x10 : 0);
        interleaved[i + 1] += ((s[i / 8] & 0x40) == 0x40 ? 0x10 : 0);
        interleaved[i + 2] += ((s[i / 8] & 0x20) == 0x20 ? 0x10 : 0);
        interleaved[i + 3] += ((s[i / 8] & 0x10) == 0x10 ? 0x10 : 0);
        interleaved[i + 4] += ((s[i / 8] & 0x08) == 0x08 ? 0x10 : 0);
        interleaved[i + 5] += ((s[i / 8] & 0x04) == 0x04 ? 0x10 : 0);
        interleaved[i + 6] += ((s[i / 8] & 0x02) == 0x02 ? 0x10 : 0);
        interleaved[i + 7] += ((s[i / 8] & 0x01) == 0x01 ? 0x10 : 0);

        interleaved[i] += ((t[i / 8] & 0x80) == 0x80 ? 0x08 : 0);
        interleaved[i + 1] += ((t[i / 8] & 0x40) == 0x40 ? 0x08 : 0);
        interleaved[i + 2] += ((t[i / 8] & 0x20) == 0x20 ? 0x08 : 0);
        interleaved[i + 3] += ((t[i / 8] & 0x10) == 0x10 ? 0x08 : 0);
        interleaved[i + 4] += ((t[i / 8] & 0x08) == 0x08 ? 0x08 : 0);
        interleaved[i + 5] += ((t[i / 8] & 0x04) == 0x04 ? 0x08 : 0);
        interleaved[i + 6] += ((t[i / 8] & 0x02) == 0x02 ? 0x08 : 0);
        interleaved[i + 7] += ((t[i / 8] & 0x01) == 0x01 ? 0x08 : 0);

        interleaved[i] += ((u[i / 8] & 0x80) == 0x80 ? 0x04 : 0);
        interleaved[i + 1] += ((u[i / 8] & 0x40) == 0x40 ? 0x04 : 0);
        interleaved[i + 2] += ((u[i / 8] & 0x20) == 0x20 ? 0x04 : 0);
        interleaved[i + 3] += ((u[i / 8] & 0x10) == 0x10 ? 0x04 : 0);
        interleaved[i + 4] += ((u[i / 8] & 0x08) == 0x08 ? 0x04 : 0);
        interleaved[i + 5] += ((u[i / 8] & 0x04) == 0x04 ? 0x04 : 0);
        interleaved[i + 6] += ((u[i / 8] & 0x02) == 0x02 ? 0x04 : 0);
        interleaved[i + 7] += ((u[i / 8] & 0x01) == 0x01 ? 0x04 : 0);

        interleaved[i] += ((v[i / 8] & 0x80) == 0x80 ? 0x02 : 0);
        interleaved[i + 1] += ((v[i / 8] & 0x40) == 0x40 ? 0x02 : 0);
        interleaved[i + 2] += ((v[i / 8] & 0x20) == 0x20 ? 0x02 : 0);
        interleaved[i + 3] += ((v[i / 8] & 0x10) == 0x10 ? 0x02 : 0);
        interleaved[i + 4] += ((v[i / 8] & 0x08) == 0x08 ? 0x02 : 0);
        interleaved[i + 5] += ((v[i / 8] & 0x04) == 0x04 ? 0x02 : 0);
        interleaved[i + 6] += ((v[i / 8] & 0x02) == 0x02 ? 0x02 : 0);
        interleaved[i + 7] += ((v[i / 8] & 0x01) == 0x01 ? 0x02 : 0);

        interleaved[i] += ((w[i / 8] & 0x80) == 0x80 ? 0x01 : 0);
        interleaved[i + 1] += ((w[i / 8] & 0x40) == 0x40 ? 0x01 : 0);
        interleaved[i + 2] += ((w[i / 8] & 0x20) == 0x20 ? 0x01 : 0);
        interleaved[i + 3] += ((w[i / 8] & 0x10) == 0x10 ? 0x01 : 0);
        interleaved[i + 4] += ((w[i / 8] & 0x08) == 0x08 ? 0x01 : 0);
        interleaved[i + 5] += ((w[i / 8] & 0x04) == 0x04 ? 0x01 : 0);
        interleaved[i + 6] += ((w[i / 8] & 0x02) == 0x02 ? 0x01 : 0);
        interleaved[i + 7] += ((w[i / 8] & 0x01) == 0x01 ? 0x01 : 0);
    }

    return AARUF_STATUS_OK;
}