# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to You under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

package Lucy::Object::I32Array;
use Lucy;
our $VERSION = '0.003002';
$VERSION = eval $VERSION;

1;

__END__

__BINDING__

my $xs_code = <<'END_XS_CODE';
MODULE = Lucy PACKAGE = Lucy::Object::I32Array

SV*
new(either_sv, ...)
    SV *either_sv;
CODE:
{
    SV *ints_sv = NULL;
    lucy_I32Array *self = NULL;

    chy_bool_t args_ok
        = XSBind_allot_params(&(ST(0)), 1, items,
                              "Lucy::Object::I32Array::new_PARAMS",
                              ALLOT_SV(&ints_sv, "ints", 4, true),
                              NULL);
    if (!args_ok) {
        CFISH_RETHROW(CFISH_INCREF(cfish_Err_get_error()));
    }

    AV *ints_av = NULL;
    if (SvROK(ints_sv)) {
        ints_av = (AV*)SvRV(ints_sv);
    }
    if (ints_av && SvTYPE(ints_av) == SVt_PVAV) {
        int32_t size  = av_len(ints_av) + 1;
        int32_t *ints = (int32_t*)LUCY_MALLOCATE(size * sizeof(int32_t));
        int32_t i;

        for (i = 0; i < size; i++) {
            SV **const sv_ptr = av_fetch(ints_av, i, 0);
            ints[i] = (sv_ptr && XSBind_sv_defined(*sv_ptr))
                      ? SvIV(*sv_ptr)
                      : 0;
        }
        self = (lucy_I32Array*)XSBind_new_blank_obj(either_sv);
        lucy_I32Arr_init(self, ints, size);
    }
    else {
        THROW(LUCY_ERR, "Required param 'ints' isn't an arrayref");
    }

    RETVAL = CFISH_OBJ_TO_SV_NOINC(self);
}
OUTPUT: RETVAL

SV*
to_arrayref(self)
    lucy_I32Array *self;
CODE:
{
    AV *out_av = newAV();
    uint32_t i;
    uint32_t size = Lucy_I32Arr_Get_Size(self);

    av_extend(out_av, size);
    for (i = 0; i < size; i++) {
        int32_t result = Lucy_I32Arr_Get(self, i);
        SV* result_sv = result == -1 ? newSV(0) : newSViv(result);
        av_push(out_av, result_sv);
    }
    RETVAL = newRV_noinc((SV*)out_av);
}
OUTPUT: RETVAL
END_XS_CODE

Clownfish::CFC::Binding::Perl::Class->register(
    parcel       => "Lucy",
    class_name   => "Lucy::Object::I32Array",
    xs_code      => $xs_code,
    bind_methods => [qw( Get Get_Size )],
);


