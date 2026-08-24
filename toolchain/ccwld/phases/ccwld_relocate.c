/* §3 relocate: relocations applied against resolved addresses, with
 * per-type statistics recorded (§9 diagnostic surface, lccwld §4.10
 * reloc_stats).  x86-64, aarch64, and riscv64 encodings cover the
 * relocation set ccwas emits (ccwas §7.3). */
#include "ccwld_phases.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static int
rs_value (ccwld_state *st, ccwld_obj *obj, const char *name, uint64_t *out,
          int *weak_undef)
{
  if (obj)
    {
      for (size_t i = 0; i < obj->nsyms; i++)
        {
          const ccwld_isym *s = &obj->syms[i];
          if (s->binding != 0 || strcmp (s->name, name))
            continue;
          if (s->shndx < 0)
            {
              *out = s->value;
              return 1;
            }
          if (s->shndx > 0)
            {
              ccwld_isec *sec = ccwld_state_isec (st, s->obj, s->shndx);
              if (!sec || !sec->placed || sec->out_sec < 0)
                return 0;
              *out = st->plan->secs[sec->out_sec].vma + sec->out_off
                     + s->value;
              return 1;
            }
          return 0;
        }
    }
  for (size_t i = 0; i < st->nrsyms; i++)
    {
      const ccwld_rsym *r = &st->rsyms[i];
      if (strcmp (r->name, name))
        continue;
      if (r->defined && r->value_known)
        {
          *out = r->value;
          return 1;
        }
      if (r->weak)
        {
          *out = 0;
          if (weak_undef)
            *weak_undef = 1;
          return 1;
        }
      return 0;
    }
  return 0;
}

static int
put_u64 (unsigned char *p, uint64_t v)
{
  for (int i = 0; i < 8; i++)
    p[i] = (unsigned char)(v >> (8 * i));
  return 1;
}

static int
patch_le (unsigned char *p, size_t width, uint64_t v)
{
  for (size_t i = 0; i < width; i++)
    p[i] = (unsigned char)((v >> (8 * i)) & 0xff);
  return 1;
}

int
ccwld_phase_relocate (ccwld_state *st, ccwld_error *e)
{
  for (size_t oi = 0; oi < st->nobjs; oi++)
    {
      ccwld_obj *o = &st->objs[oi];
      if (o->is_dso || o->is_lto)
        continue;
      for (size_t ri = 0; ri < o->nrelocs; ri++)
        {
          ccwld_ireloc *r = &o->relocs[ri];
          if (r->sec < 0 || (size_t)r->sec >= o->nsecs)
            continue;
          ccwld_isec *is = &o->secs[r->sec];
          if (!is->live || !is->placed || is->out_sec < 0)
            continue; /* dead or unplaced: no site to patch */
          if (!is->data)
            continue;

          const ccwld_sec *out = &st->plan->secs[is->out_sec];
          uint64_t P = out->vma + is->out_off + r->offset;
          uint64_t S = 0;
          int weak_undef = 0;
          if (r->sym[0])
            {
              if (!rs_value (st, o, r->sym, &S, &weak_undef))
                {
                  ccwld_error_set (e, CCWLD_EXIT_LINK,
                                   "relocation against unresolved symbol "
                                   "'%s' in '%s'",
                                   r->sym, o->path);
                  return 0;
                }
            }
          int64_t A = r->addend;

          if (r->offset >= is->size)
            {
              ccwld_error_set (e, CCWLD_EXIT_LINK,
                               "relocation offset 0x%" PRIx64
                               " outside section '%s' in '%s'",
                               r->offset, is->name, o->path);
              return 0;
            }
          unsigned char *site = is->data + r->offset;
          const char *tname = ccwld_reloc_name (o->machine, r->type);

          int ok = 1;
          switch (o->machine)
            {
            case 62: /* EM_X86_64 */
              switch (r->type)
                {
                case 1: /* R_X86_64_64: S + A */
                  if (r->offset + 8 > is->size)
                    ok = 0;
                  else
                    put_u64 (site, S + (uint64_t)A);
                  break;
                case 2:   /* PC32 */
                case 41:  /* PLT32 (same as PC32 at link time) */
                  {
                    int64_t v = (int64_t)(S + (uint64_t)A) - (int64_t)P;
                    if (r->offset + 4 > is->size
                        || v < -(int64_t)0x80000000
                        || v > (int64_t)0x7fffffff)
                      ok = 0;
                    else
                      patch_le (site, 4, (uint64_t)v);
                  }
                  break;
                case 10: /* 32 */
                  {
                    uint64_t v = S + (uint64_t)A;
                    if (r->offset + 4 > is->size || v > 0xffffffffu)
                      ok = 0;
                    else
                      patch_le (site, 4, v);
                  }
                  break;
                case 11: /* 32S */
                  {
                    int64_t v = (int64_t)(S + (uint64_t)A);
                    if (r->offset + 4 > is->size
                        || v < -(int64_t)0x80000000
                        || v > (int64_t)0x7fffffff)
                      ok = 0;
                    else
                      patch_le (site, 4, (uint64_t)v);
                  }
                  break;
                case 13: /* PC16 */
                  {
                    int64_t v = (int64_t)(S + (uint64_t)A) - (int64_t)P;
                    if (r->offset + 2 > is->size || v < -32768 || v > 32767)
                      ok = 0;
                    else
                      patch_le (site, 2, (uint64_t)v);
                  }
                  break;
                case 14: /* PC8 */
                  {
                    int64_t v = (int64_t)(S + (uint64_t)A) - (int64_t)P;
                    if (r->offset + 1 > is->size || v < -128 || v > 127)
                      ok = 0;
                    else
                      patch_le (site, 1, (uint64_t)v);
                  }
                  break;
                default:
                  ccwld_error_set (e, CCWLD_EXIT_LINK,
                                   "unsupported relocation %s in '%s'",
                                   tname, o->path);
                  return 0;
                }
              break;
            case 183: /* EM_AARCH64 */
              switch (r->type)
                {
                case 257: /* ABS64 */
                  if (r->offset + 8 > is->size)
                    ok = 0;
                  else
                    put_u64 (site, S + (uint64_t)A);
                  break;
                case 283: /* CALL26: ((S+A-P) >> 2) & 0x03ffffff */
                  {
                    int64_t off = (int64_t)(S + (uint64_t)A) - (int64_t)P;
                    if (r->offset + 4 > is->size || (off & 3)
                        || off < -(1 << 27) || off >= (1 << 27))
                      ok = 0;
                    else
                      {
                        uint32_t insn;
                        memcpy (&insn, site, 4);
                        uint32_t imm = (uint32_t) ((off >> 2) & 0x03ffffff);
                        insn = (insn & 0xfc000000u) | imm;
                        memcpy (site, &insn, 4);
                      }
                  }
                  break;
                case 275: /* ADR_PREL_PG_HI21 */
                  {
                    uint64_t va = (S + (uint64_t)A) & ~0xfffull;
                    uint64_t pa = P & ~0xfffull;
                    int64_t off = (int64_t)(va - pa);
                    if (r->offset + 4 > is->size)
                      ok = 0;
                    else
                      {
                        uint32_t imm = (uint32_t)((off >> 12) & 0x1fffff);
                        uint32_t insn;
                        memcpy (&insn, site, 4);
                        insn = (insn & ~0x60ffffe0u)
                               | ((imm & 3) << 29)
                               | ((imm & 0x1ffffc) << 3);
                        memcpy (site, &insn, 4);
                      }
                  }
                  break;
                case 277: /* ADD_ABS_LO12_NC */
                  {
                    uint64_t v = (S + (uint64_t)A) & 0xfffu;
                    if (r->offset + 4 > is->size)
                      ok = 0;
                    else
                      {
                        uint32_t insn;
                        memcpy (&insn, site, 4);
                        insn = (insn & ~0xfffu) | ((uint32_t)v << 10);
                        memcpy (site, &insn, 4);
                      }
                  }
                  break;
                default:
                  ccwld_error_set (e, CCWLD_EXIT_LINK,
                                   "unsupported relocation %s in '%s'",
                                   tname, o->path);
                  return 0;
                }
              break;
            case 243: /* EM_RISCV */
              switch (r->type)
                {
                case 2: /* RISCV_64 */
                  if (r->offset + 8 > is->size)
                    ok = 0;
                  else
                    put_u64 (site, S + (uint64_t)A);
                  break;
                case 23: /* PCREL_HI20 */
                  {
                    int64_t off = (int64_t)(S + (uint64_t)A) - (int64_t)P;
                    if (r->offset + 4 > is->size
                        || off > INT64_C (2147483647)
                        || off < -INT64_C (2147483648))
                      ok = 0;
                    else
                      {
                        uint32_t imm = (uint32_t)((off + 0x800) >> 12)
                                       & 0xfffff;
                        uint32_t insn;
                        memcpy (&insn, site, 4);
                        insn = (insn & 0x00000fffu) | (imm << 12);
                        memcpy (site, &insn, 4);
                      }
                  }
                  break;
                case 24: /* PCREL_LO12_I */
                case 25: /* PCREL_LO12_S */
                  /* resolved through the paired HI20 label; the pair is
                   * validated by ccwas (§7.3).  The LO12 addend carries
                   * the HI20 site offset; recompute from S - HI20_P. */
                  {
                    int64_t off = (int64_t)(S + (uint64_t)A) - (int64_t)P;
                    uint32_t imm = (uint32_t)(off & 0xfff);
                    if (r->offset + 4 > is->size)
                      ok = 0;
                    else
                      {
                        uint32_t insn;
                        memcpy (&insn, site, 4);
                        if (r->type == 24)
                          insn = (insn & 0xfffff0ffu) | (imm << 20);
                        else
                          insn = (insn & 0xfe0fffffu) | ((imm & 0x1f) << 25)
                                 | ((imm >> 5) << 7);
                        memcpy (site, &insn, 4);
                      }
                  }
                  break;
                default:
                  ccwld_error_set (e, CCWLD_EXIT_LINK,
                                   "unsupported relocation %s in '%s'",
                                   tname, o->path);
                  return 0;
                }
              break;
            default:
              ccwld_error_set (e, CCWLD_EXIT_LINK,
                               "cannot relocate: unknown machine %d in '%s'",
                               o->machine, o->path);
              return 0;
            }

          if (!ok)
            {
              st->reloc_errors++;
              ccwld_error_set (e, CCWLD_EXIT_LINK,
                               "relocation %s at 0x%" PRIx64
                               " in '%s' does not fit its site",
                               tname, r->offset, o->path);
              return 0;
            }
          ccwld_state_record_stat (st, tname);
        }
    }
  return 1;
}
