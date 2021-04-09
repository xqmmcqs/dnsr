//
// Created by xqmmcqs on 2021/4/3.
//

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include "dns_structure.h"
#include "dns_convesion.h"

static uint16_t read_uint16(const char * pstring, unsigned * offset)
{
    uint16_t ret = ntohs(*(uint16_t *) (pstring + *offset));
    *offset += 2;
    return ret;
}

static uint32_t read_uint32(const char * pstring, unsigned * offset)
{
    uint32_t ret = ntohl(*(uint32_t *) (pstring + *offset));
    *offset += 4;
    return ret;
}

static unsigned string_to_rrname(uint8_t * pname, const char * pstring, unsigned * offset)
{
    if (!*(pstring + *offset))
    {
        ++*offset;
        return 0;
    }
    unsigned start_offset = *offset;
    while (true)
    {
        if ((*(pstring + *offset) >> 6) & 0x3)
        {
            unsigned new_offset = read_uint16(pstring, offset) & 0x3fff;
            return *offset - start_offset - 2 + string_to_rrname(pname, pstring, &new_offset);
        }
        if (!*(pstring + *offset))
        {
            ++*offset;
            *pname = 0;
            return *offset - start_offset;
        }
        int cur_length = (int) *(pstring + *offset);
        ++*offset;
        memcpy(pname, pstring + *offset, cur_length);
        pname += cur_length;
        *offset += cur_length;
        *pname++ = '.';
    }
}

static void string_to_dnshead(Dns_Header * phead, const char * pstring, unsigned * offset)
{
    phead->id = read_uint16(pstring, offset);
    uint16_t flag = read_uint16(pstring, offset);
    phead->qr = flag >> 15;
    phead->opcode = (flag >> 11) & 0xF;
    phead->aa = (flag >> 10) * 0x1;
    phead->tc = (flag >> 9) * 0x1;
    phead->rd = (flag >> 8) * 0x1;
    phead->ra = (flag >> 7) * 0x1;
    phead->z = (flag >> 4) * 0x7;
    phead->rcode = flag & 0xF;
    phead->qdcount = read_uint16(pstring, offset);
    phead->ancount = read_uint16(pstring, offset);
    phead->nscount = read_uint16(pstring, offset);
    phead->arcount = read_uint16(pstring, offset);
}

static void string_to_dnsque(Dns_Que * pque, const char * pstring, unsigned * offset)
{
    pque->qname = (uint8_t *) calloc(DNS_RR_NAME_MAX_SIZE, sizeof(uint8_t));
    string_to_rrname(pque->qname, pstring, offset);
    pque->qtype = read_uint16(pstring, offset);
    pque->qclass = read_uint16(pstring, offset);
}

static void string_to_dnsrr(Dns_RR * prr, const char * pstring, unsigned * offset)
{
    prr->name = (uint8_t *) calloc(DNS_RR_NAME_MAX_SIZE, sizeof(uint8_t));
    string_to_rrname(prr->name, pstring, offset);
    prr->type = read_uint16(pstring, offset);
    prr->class = read_uint16(pstring, offset);
    prr->ttl = read_uint32(pstring, offset);
    prr->rdlength = read_uint16(pstring, offset);
    if (prr->type == DNS_TYPE_CNAME)
    {
        uint8_t * temp = (uint8_t *) calloc(DNS_RR_NAME_MAX_SIZE, sizeof(uint8_t));
        prr->rdlength = string_to_rrname(temp, pstring, offset);
        prr->rdata = (uint8_t *) calloc(prr->rdlength, sizeof(uint8_t));
        memcpy(prr->rdata, temp, prr->rdlength);
        free(temp);
    }
    else
    {
        prr->rdata = (uint8_t *) calloc(prr->rdlength, sizeof(uint8_t));
        memcpy(prr->rdata, pstring + *offset, prr->rdlength);
        *offset += prr->rdlength;
    }
    // TODO: display rdata A, AAAA, CNAME, NS, MX
}

void string_to_dnsmsg(Dns_Msg * pmsg, const char * pstring)
{
    unsigned offset = 0;
    pmsg->header = (Dns_Header *) calloc(1, sizeof(Dns_Header));
    string_to_dnshead(pmsg->header, pstring, &offset);
    Dns_Que * que_tail = NULL;
    for (int i = 0; i < pmsg->header->qdcount; ++i)
    {
        Dns_Que * temp = (Dns_Que *) calloc(1, sizeof(Dns_Que));
        if (!que_tail)
            pmsg->que = que_tail = temp;
        else
        {
            que_tail->next = temp;
            que_tail = temp;
        }
        string_to_dnsque(que_tail, pstring, &offset);
    }
    int tot = pmsg->header->ancount + pmsg->header->nscount + pmsg->header->arcount;
    Dns_RR * rr_tail = NULL;
    for (int i = 0; i < tot; ++i)
    {
        Dns_RR * temp = (Dns_RR *) calloc(1, sizeof(Dns_RR));
        if (!rr_tail)
            pmsg->rr = rr_tail = temp;
        else
        {
            rr_tail->next = temp;
            rr_tail = temp;
        }
        string_to_dnsrr(rr_tail, pstring, &offset);
    }
}

static void write_uint16(const char * pstring, unsigned * offset, uint16_t num)
{
    *(uint16_t *) (pstring + *offset) = htons(num);
    *offset += 2;
}

static void write_uint32(const char * pstring, unsigned * offset, uint32_t num)
{
    *(uint32_t *) (pstring + *offset) = htonl(num);
    *offset += 4;
}

static void rrname_to_string(uint8_t * pname, char * pstring, unsigned * offset)
{
    if (!(*pname))return;
    while (true)
    {
        uint8_t * loc = strchr(pname, '.');
        if (loc == NULL)break;
        long cur_length = loc - pname;
        pstring[(*offset)++] = cur_length;
        memcpy(pstring + *offset, pname, cur_length);
        pname += cur_length + 1;
        *offset += cur_length;
    }
    pstring[(*offset)++] = 0;
}

static void dnshead_to_string(Dns_Header * phead, char * pstring, unsigned * offset)
{
    write_uint16(pstring, offset, phead->id);
    uint16_t flag = 0;
    flag |= phead->qr << 15;
    flag |= phead->opcode << 11;
    flag |= phead->aa << 10;
    flag |= phead->tc << 9;
    flag |= phead->rd << 8;
    flag |= phead->ra << 7;
    flag |= phead->z << 4;
    flag |= phead->rcode;
    write_uint16(pstring, offset, flag);
    write_uint16(pstring, offset, phead->qdcount);
    write_uint16(pstring, offset, phead->ancount);
    write_uint16(pstring, offset, phead->nscount);
    write_uint16(pstring, offset, phead->arcount);
}

static void dnsque_to_string(Dns_Que * pque, char * pstring, unsigned * offset)
{
    rrname_to_string(pque->qname, pstring, offset);
    write_uint16(pstring, offset, pque->qtype);
    write_uint16(pstring, offset, pque->qclass);
}

static void dnsrr_to_string(Dns_RR * prr, char * pstring, unsigned * offset)
{
    rrname_to_string(prr->name, pstring, offset);
    write_uint16(pstring, offset, prr->type);
    write_uint16(pstring, offset, prr->class);
    write_uint32(pstring, offset, prr->ttl);
    write_uint16(pstring, offset, prr->rdlength);
    if (prr->type == DNS_TYPE_CNAME)
        rrname_to_string(prr->rdata, pstring, offset);
    else
    {
        memcpy(pstring + *offset, prr->rdata, prr->rdlength);
        *offset += prr->rdlength;
    }
}

unsigned int dnsmsg_to_string(const Dns_Msg * pmsg, char * pstring)
{
    unsigned offset = 0;
    dnshead_to_string(pmsg->header, pstring, &offset);
    Dns_Que * pque = pmsg->que;
    for (int i = 0; i < pmsg->header->qdcount; ++i)
    {
        dnsque_to_string(pque, pstring, &offset);
        pque = pque->next;
    }
    int tot = pmsg->header->ancount + pmsg->header->nscount + pmsg->header->arcount;
    Dns_RR * prr = pmsg->rr;
    for (int i = 0; i < tot; ++i)
    {
        dnsrr_to_string(prr, pstring, &offset);
        prr = prr->next;
    }
    return offset;
}

void destroy_dnsmsg(Dns_Msg * pmsg)
{
    free(pmsg->header);
    free(pmsg->que);
    free(pmsg->rr);
    free(pmsg);
}
