/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define C_LUCY_OBJ
#define NEED_newRV_noinc
#include "XSBind.h"
#include "Lucy/Util/StringHelper.h"

// Convert a Perl hash into a Clownfish Hash.  Caller takes responsibility for
// a refcount.
static cfish_Hash*
S_perl_hash_to_cfish_hash(HV *phash);

// Convert a Perl array into a Clownfish VArray.  Caller takes responsibility
// for a refcount.
static cfish_VArray*
S_perl_array_to_cfish_array(AV *parray);

// Convert a VArray to a Perl array.  Caller takes responsibility for a
// refcount.
static SV*
S_cfish_array_to_perl_array(cfish_VArray *varray);

// Convert a Hash to a Perl hash.  Caller takes responsibility for a refcount.
static SV*
S_cfish_hash_to_perl_hash(cfish_Hash *hash);

cfish_Obj*
XSBind_new_blank_obj(SV *either_sv) {
    cfish_VTable *vtable;

    // Get a VTable.
    if (sv_isobject(either_sv)
        && sv_derived_from(either_sv, "Lucy::Object::Obj")
       ) {
        // Use the supplied object's VTable.
        IV iv_ptr = SvIV(SvRV(either_sv));
        cfish_Obj *self = INT2PTR(cfish_Obj*, iv_ptr);
        vtable = self->vtable;
    }
    else {
        // Use the supplied class name string to find a VTable.
        STRLEN len;
        char *ptr = SvPVutf8(either_sv, len);
        cfish_ZombieCharBuf *klass = CFISH_ZCB_WRAP_STR(ptr, len);
        vtable = cfish_VTable_singleton((cfish_CharBuf*)klass, NULL);
    }

    // Use the VTable to allocate a new blank object of the right size.
    return Cfish_VTable_Make_Obj(vtable);
}

cfish_Obj*
XSBind_sv_to_cfish_obj(SV *sv, cfish_VTable *vtable, void *allocation) {
    cfish_Obj *retval = XSBind_maybe_sv_to_cfish_obj(sv, vtable, allocation);
    if (!retval) {
        THROW(CFISH_ERR, "Not a %o", Cfish_VTable_Get_Name(vtable));
    }
    return retval;
}

cfish_Obj*
XSBind_maybe_sv_to_cfish_obj(SV *sv, cfish_VTable *vtable, void *allocation) {
    cfish_Obj *retval = NULL;
    if (XSBind_sv_defined(sv)) {
        if (sv_isobject(sv)
            && sv_derived_from(sv, (char*)Cfish_CB_Get_Ptr8(Cfish_VTable_Get_Name(vtable)))
           ) {
            // Unwrap a real Clownfish object.
            IV tmp = SvIV(SvRV(sv));
            retval = INT2PTR(cfish_Obj*, tmp);
        }
        else if (allocation &&
                 (vtable == CFISH_ZOMBIECHARBUF
                  || vtable == CFISH_VIEWCHARBUF
                  || vtable == CFISH_CHARBUF
                  || vtable == CFISH_OBJ)
                ) {
            // Wrap the string from an ordinary Perl scalar inside a
            // ZombieCharBuf.
            STRLEN size;
            char *ptr = SvPVutf8(sv, size);
            retval = (cfish_Obj*)cfish_ZCB_wrap_str(allocation, ptr, size);
        }
        else if (SvROK(sv)) {
            // Attempt to convert Perl hashes and arrays into their Clownfish
            // analogues.
            SV *inner = SvRV(sv);
            if (SvTYPE(inner) == SVt_PVAV && vtable == CFISH_VARRAY) {
                retval = (cfish_Obj*)S_perl_array_to_cfish_array((AV*)inner);
            }
            else if (SvTYPE(inner) == SVt_PVHV && vtable == CFISH_HASH) {
                retval = (cfish_Obj*)S_perl_hash_to_cfish_hash((HV*)inner);
            }

            if (retval) {
                // Mortalize the converted object -- which is somewhat
                // dangerous, but is the only way to avoid requiring that the
                // caller take responsibility for a refcount.
                SV *mortal = (SV*)Cfish_Obj_To_Host(retval);
                CFISH_DECREF(retval);
                sv_2mortal(mortal);
            }
        }
    }

    return retval;
}

SV*
XSBind_cfish_to_perl(cfish_Obj *obj) {
    if (obj == NULL) {
        return newSV(0);
    }
    else if (Cfish_Obj_Is_A(obj, CFISH_CHARBUF)) {
        return XSBind_cb_to_sv((cfish_CharBuf*)obj);
    }
    else if (Cfish_Obj_Is_A(obj, CFISH_BYTEBUF)) {
        return XSBind_bb_to_sv((cfish_ByteBuf*)obj);
    }
    else if (Cfish_Obj_Is_A(obj, CFISH_VARRAY)) {
        return S_cfish_array_to_perl_array((cfish_VArray*)obj);
    }
    else if (Cfish_Obj_Is_A(obj, CFISH_HASH)) {
        return S_cfish_hash_to_perl_hash((cfish_Hash*)obj);
    }
    else if (Cfish_Obj_Is_A(obj, CFISH_FLOATNUM)) {
        return newSVnv(Cfish_Obj_To_F64(obj));
    }
    else if (obj == (cfish_Obj*)CFISH_TRUE) {
        return newSViv(1);
    }
    else if (obj == (cfish_Obj*)CFISH_FALSE) {
        return newSViv(0);
    }
    else if (sizeof(IV) == 8 && Cfish_Obj_Is_A(obj, CFISH_INTNUM)) {
        int64_t num = Cfish_Obj_To_I64(obj);
        return newSViv((IV)num);
    }
    else if (sizeof(IV) == 4 && Cfish_Obj_Is_A(obj, CFISH_INTEGER32)) {
        int32_t num = (int32_t)Cfish_Obj_To_I64(obj);
        return newSViv((IV)num);
    }
    else if (sizeof(IV) == 4 && Cfish_Obj_Is_A(obj, CFISH_INTEGER64)) {
        int64_t num = Cfish_Obj_To_I64(obj);
        return newSVnv((double)num); // lossy
    }
    else {
        return (SV*)Cfish_Obj_To_Host(obj);
    }
}

cfish_Obj*
XSBind_perl_to_cfish(SV *sv) {
    cfish_Obj *retval = NULL;

    if (XSBind_sv_defined(sv)) {
        if (SvROK(sv)) {
            // Deep conversion of references.
            SV *inner = SvRV(sv);
            if (SvTYPE(inner) == SVt_PVAV) {
                retval = (cfish_Obj*)S_perl_array_to_cfish_array((AV*)inner);
            }
            else if (SvTYPE(inner) == SVt_PVHV) {
                retval = (cfish_Obj*)S_perl_hash_to_cfish_hash((HV*)inner);
            }
            else if (sv_isobject(sv)
                     && sv_derived_from(sv, "Lucy::Object::Obj")
                    ) {
                IV tmp = SvIV(inner);
                retval = INT2PTR(cfish_Obj*, tmp);
                (void)CFISH_INCREF(retval);
            }
        }

        // It's either a plain scalar or a non-Clownfish Perl object, so
        // stringify.
        if (!retval) {
            STRLEN len;
            char *ptr = SvPVutf8(sv, len);
            retval = (cfish_Obj*)cfish_CB_new_from_trusted_utf8(ptr, len);
        }
    }
    else if (sv) {
        // Deep conversion of raw AVs and HVs.
        if (SvTYPE(sv) == SVt_PVAV) {
            retval = (cfish_Obj*)S_perl_array_to_cfish_array((AV*)sv);
        }
        else if (SvTYPE(sv) == SVt_PVHV) {
            retval = (cfish_Obj*)S_perl_hash_to_cfish_hash((HV*)sv);
        }
    }

    return retval;
}

SV*
XSBind_bb_to_sv(const cfish_ByteBuf *bb) {
    return bb
           ? newSVpvn(Cfish_BB_Get_Buf(bb), Cfish_BB_Get_Size(bb))
           : newSV(0);
}

SV*
XSBind_cb_to_sv(const cfish_CharBuf *cb) {
    if (!cb) {
        return newSV(0);
    }
    else {
        SV *sv = newSVpvn((char*)Cfish_CB_Get_Ptr8(cb), Cfish_CB_Get_Size(cb));
        SvUTF8_on(sv);
        return sv;
    }
}

static cfish_Hash*
S_perl_hash_to_cfish_hash(HV *phash) {
    uint32_t             num_keys = hv_iterinit(phash);
    cfish_Hash          *retval   = cfish_Hash_new(num_keys);
    cfish_ZombieCharBuf *key      = CFISH_ZCB_WRAP_STR("", 0);

    while (num_keys--) {
        HE        *entry    = hv_iternext(phash);
        STRLEN     key_len  = HeKLEN(entry);
        SV        *value_sv = HeVAL(entry);
        cfish_Obj *value    = XSBind_perl_to_cfish(value_sv); // Recurse.

        // Force key to UTF-8 if necessary.
        if (key_len == (STRLEN)HEf_SVKEY) {
            // Key is stored as an SV.  Use its UTF-8 flag?  Not sure about
            // this.
            SV   *key_sv  = HeKEY_sv(entry);
            char *key_str = SvPVutf8(key_sv, key_len);
            Cfish_ZCB_Assign_Trusted_Str(key, key_str, key_len);
            Cfish_Hash_Store(retval, (cfish_Obj*)key, value);
        }
        else if (HeKUTF8(entry)) {
            Cfish_ZCB_Assign_Trusted_Str(key, HeKEY(entry), key_len);
            Cfish_Hash_Store(retval, (cfish_Obj*)key, value);
        }
        else {
            char *key_str = HeKEY(entry);
            chy_bool_t pure_ascii = true;
            for (STRLEN i = 0; i < key_len; i++) {
                if ((key_str[i] & 0x80) == 0x80) { pure_ascii = false; }
            }
            if (pure_ascii) {
                Cfish_ZCB_Assign_Trusted_Str(key, key_str, key_len);
                Cfish_Hash_Store(retval, (cfish_Obj*)key, value);
            }
            else {
                SV *key_sv = HeSVKEY_force(entry);
                key_str = SvPVutf8(key_sv, key_len);
                Cfish_ZCB_Assign_Trusted_Str(key, key_str, key_len);
                Cfish_Hash_Store(retval, (cfish_Obj*)key, value);
            }
        }
    }

    return retval;
}

static cfish_VArray*
S_perl_array_to_cfish_array(AV *parray) {
    const uint32_t  size   = av_len(parray) + 1;
    cfish_VArray   *retval = cfish_VA_new(size);
    uint32_t i;

    // Iterate over array elems.
    for (i = 0; i < size; i++) {
        SV **elem_sv = av_fetch(parray, i, false);
        if (elem_sv) {
            cfish_Obj *elem = XSBind_perl_to_cfish(*elem_sv);
            if (elem) { Cfish_VA_Store(retval, i, elem); }
        }
    }
    Cfish_VA_Resize(retval, size); // needed if last elem is NULL

    return retval;
}

static SV*
S_cfish_array_to_perl_array(cfish_VArray *varray) {
    AV *perl_array = newAV();
    uint32_t num_elems = Cfish_VA_Get_Size(varray);

    // Iterate over array elems.
    if (num_elems) {
        uint32_t i;
        av_fill(perl_array, num_elems - 1);
        for (i = 0; i < num_elems; i++) {
            cfish_Obj *val = Cfish_VA_Fetch(varray, i);
            if (val == NULL) {
                continue;
            }
            else {
                // Recurse for each value.
                SV *const val_sv = XSBind_cfish_to_perl(val);
                av_store(perl_array, i, val_sv);
            }
        }
    }

    return newRV_noinc((SV*)perl_array);
}

static SV*
S_cfish_hash_to_perl_hash(cfish_Hash *hash) {
    HV *perl_hash = newHV();
    SV *key_sv    = newSV(1);
    cfish_CharBuf *key;
    cfish_Obj     *val;

    // Prepare the SV key.
    SvPOK_on(key_sv);
    SvUTF8_on(key_sv);

    // Iterate over key-value pairs.
    Cfish_Hash_Iterate(hash);
    while (Cfish_Hash_Next(hash, (cfish_Obj**)&key, &val)) {
        // Recurse for each value.
        SV *val_sv = XSBind_cfish_to_perl(val);
        if (!Cfish_Obj_Is_A((cfish_Obj*)key, CFISH_CHARBUF)) {
            CFISH_THROW(CFISH_ERR,
                        "Can't convert a key of class %o to a Perl hash key",
                        Cfish_Obj_Get_Class_Name((cfish_Obj*)key));
        }
        else {
            STRLEN key_size = Cfish_CB_Get_Size(key);
            char *key_sv_ptr = SvGROW(key_sv, key_size + 1);
            memcpy(key_sv_ptr, Cfish_CB_Get_Ptr8(key), key_size);
            SvCUR_set(key_sv, key_size);
            *SvEND(key_sv) = '\0';
            (void)hv_store_ent(perl_hash, key_sv, val_sv, 0);
        }
    }
    SvREFCNT_dec(key_sv);

    return newRV_noinc((SV*)perl_hash);
}

void
XSBind_enable_overload(void *pobj) {
    SV *perl_obj = (SV*)pobj;
    HV *stash = SvSTASH(SvRV(perl_obj));
#if (PERL_VERSION > 10)
    Gv_AMupdate(stash, false);
#else
    Gv_AMupdate(stash);
#endif
    SvAMAGIC_on(perl_obj);
}

static chy_bool_t
S_extract_from_sv(SV *value, void *target, const char *label,
                  chy_bool_t required, int type, cfish_VTable *vtable,
                  void *allocation) {
    chy_bool_t valid_assignment = false;

    if (XSBind_sv_defined(value)) {
        switch (type) {
            case XSBIND_WANT_I8:
                *((int8_t*)target) = (int8_t)SvIV(value);
                valid_assignment = true;
                break;
            case XSBIND_WANT_I16:
                *((int16_t*)target) = (int16_t)SvIV(value);
                valid_assignment = true;
                break;
            case XSBIND_WANT_I32:
                *((int32_t*)target) = (int32_t)SvIV(value);
                valid_assignment = true;
                break;
            case XSBIND_WANT_I64:
                if (sizeof(IV) == 8) {
                    *((int64_t*)target) = (int64_t)SvIV(value);
                }
                else { // sizeof(IV) == 4
                    // lossy.
                    *((int64_t*)target) = (int64_t)SvNV(value);
                }
                valid_assignment = true;
                break;
            case XSBIND_WANT_U8:
                *((uint8_t*)target) = (uint8_t)SvUV(value);
                valid_assignment = true;
                break;
            case XSBIND_WANT_U16:
                *((uint16_t*)target) = (uint16_t)SvUV(value);
                valid_assignment = true;
                break;
            case XSBIND_WANT_U32:
                *((uint32_t*)target) = (uint32_t)SvUV(value);
                valid_assignment = true;
                break;
            case XSBIND_WANT_U64:
                if (sizeof(UV) == 8) {
                    *((uint64_t*)target) = (uint64_t)SvUV(value);
                }
                else { // sizeof(UV) == 4
                    // lossy.
                    *((uint64_t*)target) = (uint64_t)SvNV(value);
                }
                valid_assignment = true;
                break;
            case XSBIND_WANT_BOOL:
                *((chy_bool_t*)target) = !!SvTRUE(value);
                valid_assignment = true;
                break;
            case XSBIND_WANT_F32:
                *((float*)target) = (float)SvNV(value);
                valid_assignment = true;
                break;
            case XSBIND_WANT_F64:
                *((double*)target) = SvNV(value);
                valid_assignment = true;
                break;
            case XSBIND_WANT_OBJ: {
                    cfish_Obj *object
                        = XSBind_maybe_sv_to_cfish_obj(value, vtable,
                                                       allocation);
                    if (object) {
                        *((cfish_Obj**)target) = object;
                        valid_assignment = true;
                    }
                    else {
                        cfish_CharBuf *mess
                            = CFISH_MAKE_MESS(
                                  "Invalid value for '%s' - not a %o",
                                  label, Cfish_VTable_Get_Name(vtable));
                        cfish_Err_set_error(cfish_Err_new(mess));
                        return false;
                    }
                }
                break;
            case XSBIND_WANT_SV:
                *((SV**)target) = value;
                valid_assignment = true;
                break;
            default: {
                    cfish_CharBuf *mess
                        = CFISH_MAKE_MESS("Unrecognized type: %i32 for param '%s'",
                                          (int32_t)type, label);
                    cfish_Err_set_error(cfish_Err_new(mess));
                    return false;
                }
        }
    }

    // Enforce that required params cannot be undef and must present valid
    // values.
    if (required && !valid_assignment) {
        cfish_CharBuf *mess = CFISH_MAKE_MESS("Missing required param %s",
                                              label);
        cfish_Err_set_error(cfish_Err_new(mess));
        return false;
    }

    return true;
}

chy_bool_t
XSBind_allot_params(SV** stack, int32_t start, int32_t num_stack_elems,
                    char* params_hash_name, ...) {
    va_list args;
    HV *params_hash = get_hv(params_hash_name, 0);
    int32_t args_left = (num_stack_elems - start) / 2;

    // Retrieve the params hash, which must be a package global.
    if (params_hash == NULL) {
        cfish_CharBuf *mess = CFISH_MAKE_MESS("Can't find hash named %s",
                                              params_hash_name);
        cfish_Err_set_error(cfish_Err_new(mess));
        return false;
    }

    // Verify that our args come in pairs. Return success if there are no
    // args.
    if ((num_stack_elems - start) % 2 != 0) {
        cfish_CharBuf *mess
            = CFISH_MAKE_MESS(
                  "Expecting hash-style params, got odd number of args");
        cfish_Err_set_error(cfish_Err_new(mess));
        return false;
    }

    // Validate param names.
    for (int32_t i = start; i < num_stack_elems; i += 2) {
        SV *const key_sv = stack[i];
        STRLEN key_len;
        const char *key = SvPV(key_sv, key_len); // assume ASCII labels
        if (!hv_exists(params_hash, key, key_len)) {
            cfish_CharBuf *mess
                = CFISH_MAKE_MESS("Invalid parameter: '%s'", key);
            cfish_Err_set_error(cfish_Err_new(mess));
            return false;
        }
    }

    void *target;
    va_start(args, params_hash_name);
    while (NULL != (target = va_arg(args, void*))) {
        char *label     = va_arg(args, char*);
        int   label_len = va_arg(args, int);
        int   required  = va_arg(args, int);
        int   type      = va_arg(args, int);
        cfish_VTable *vtable = va_arg(args, cfish_VTable*);
        void *allocation = va_arg(args, void*);

        // Iterate through stack looking for a label match. Work backwards so
        // that if the label is doubled up we get the last one.
        chy_bool_t got_arg = false;
        for (int32_t i = num_stack_elems; i >= start + 2; i -= 2) {
            int32_t tick = i - 2;
            SV *const key_sv = stack[tick];
            if (SvCUR(key_sv) == (STRLEN)label_len) {
                if (memcmp(SvPVX(key_sv), label, label_len) == 0) {
                    SV *value = stack[tick + 1];
                    got_arg = S_extract_from_sv(value, target, label,
                                                required, type, vtable,
                                                allocation);
                    if (!got_arg) {
                        CFISH_ERR_ADD_FRAME(cfish_Err_get_error());
                        return false;
                    }
                    break;
                }
            }
        }

        // Enforce required params.
        if (required && !got_arg) {
            cfish_CharBuf *mess
                = CFISH_MAKE_MESS("Missing required parameter: '%s'", label);
            cfish_Err_set_error(cfish_Err_new(mess));
            return false;
        }
    }
    va_end(args);

    return true;
}


