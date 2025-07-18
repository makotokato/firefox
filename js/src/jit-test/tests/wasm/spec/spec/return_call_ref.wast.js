/* Copyright 2021 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// ./test/core/return_call_ref.wast

// ./test/core/return_call_ref.wast:3
let $0 = instantiate(`(module
  ;; Auxiliary definitions
  (type \$proc (func))
  (type \$-i32 (func (result i32)))
  (type \$-i64 (func (result i64)))
  (type \$-f32 (func (result f32)))
  (type \$-f64 (func (result f64)))

  (type \$i32-i32 (func (param i32) (result i32)))
  (type \$i64-i64 (func (param i64) (result i64)))
  (type \$f32-f32 (func (param f32) (result f32)))
  (type \$f64-f64 (func (param f64) (result f64)))

  (type \$f32-i32 (func (param f32 i32) (result i32)))
  (type \$i32-i64 (func (param i32 i64) (result i64)))
  (type \$f64-f32 (func (param f64 f32) (result f32)))
  (type \$i64-f64 (func (param i64 f64) (result f64)))

  (type \$i64i64-i64 (func (param i64 i64) (result i64)))

  (func \$const-i32 (result i32) (i32.const 0x132))
  (func \$const-i64 (result i64) (i64.const 0x164))
  (func \$const-f32 (result f32) (f32.const 0xf32))
  (func \$const-f64 (result f64) (f64.const 0xf64))

  (func \$id-i32 (param i32) (result i32) (local.get 0))
  (func \$id-i64 (param i64) (result i64) (local.get 0))
  (func \$id-f32 (param f32) (result f32) (local.get 0))
  (func \$id-f64 (param f64) (result f64) (local.get 0))

  (func \$f32-i32 (param f32 i32) (result i32) (local.get 1))
  (func \$i32-i64 (param i32 i64) (result i64) (local.get 1))
  (func \$f64-f32 (param f64 f32) (result f32) (local.get 1))
  (func \$i64-f64 (param i64 f64) (result f64) (local.get 1))

  (global \$const-i32 (ref \$-i32) (ref.func \$const-i32))
  (global \$const-i64 (ref \$-i64) (ref.func \$const-i64))
  (global \$const-f32 (ref \$-f32) (ref.func \$const-f32))
  (global \$const-f64 (ref \$-f64) (ref.func \$const-f64))

  (global \$id-i32 (ref \$i32-i32) (ref.func \$id-i32))
  (global \$id-i64 (ref \$i64-i64) (ref.func \$id-i64))
  (global \$id-f32 (ref \$f32-f32) (ref.func \$id-f32))
  (global \$id-f64 (ref \$f64-f64) (ref.func \$id-f64))

  (global \$f32-i32 (ref \$f32-i32) (ref.func \$f32-i32))
  (global \$i32-i64 (ref \$i32-i64) (ref.func \$i32-i64))
  (global \$f64-f32 (ref \$f64-f32) (ref.func \$f64-f32))
  (global \$i64-f64 (ref \$i64-f64) (ref.func \$i64-f64))

  (elem declare func
    \$const-i32 \$const-i64 \$const-f32 \$const-f64
    \$id-i32 \$id-i64 \$id-f32 \$id-f64
    \$f32-i32 \$i32-i64 \$f64-f32 \$i64-f64
  )

  ;; Typing

  (func (export "type-i32") (result i32)
    (return_call_ref \$-i32 (global.get \$const-i32))
  )
  (func (export "type-i64") (result i64)
    (return_call_ref \$-i64 (global.get \$const-i64))
  )
  (func (export "type-f32") (result f32)
    (return_call_ref \$-f32 (global.get \$const-f32))
  )
  (func (export "type-f64") (result f64)
    (return_call_ref \$-f64 (global.get \$const-f64))
  )

  (func (export "type-first-i32") (result i32)
    (return_call_ref \$i32-i32 (i32.const 32) (global.get \$id-i32))
  )
  (func (export "type-first-i64") (result i64)
    (return_call_ref \$i64-i64 (i64.const 64) (global.get \$id-i64))
  )
  (func (export "type-first-f32") (result f32)
    (return_call_ref \$f32-f32 (f32.const 1.32) (global.get \$id-f32))
  )
  (func (export "type-first-f64") (result f64)
    (return_call_ref \$f64-f64 (f64.const 1.64) (global.get \$id-f64))
  )

  (func (export "type-second-i32") (result i32)
    (return_call_ref \$f32-i32 (f32.const 32.1) (i32.const 32) (global.get \$f32-i32))
  )
  (func (export "type-second-i64") (result i64)
    (return_call_ref \$i32-i64 (i32.const 32) (i64.const 64) (global.get \$i32-i64))
  )
  (func (export "type-second-f32") (result f32)
    (return_call_ref \$f64-f32 (f64.const 64) (f32.const 32) (global.get \$f64-f32))
  )
  (func (export "type-second-f64") (result f64)
    (return_call_ref \$i64-f64 (i64.const 64) (f64.const 64.1) (global.get \$i64-f64))
  )

  ;; Null

  (func (export "null")
    (return_call_ref \$proc (ref.null \$proc))
  )

  ;; Recursion

  (global \$fac-acc (ref \$i64i64-i64) (ref.func \$fac-acc))

  (elem declare func \$fac-acc)
  (func \$fac-acc (export "fac-acc") (param i64 i64) (result i64)
    (if (result i64) (i64.eqz (local.get 0))
      (then (local.get 1))
      (else
        (return_call_ref \$i64i64-i64
          (i64.sub (local.get 0) (i64.const 1))
          (i64.mul (local.get 0) (local.get 1))
          (global.get \$fac-acc)
        )
      )
    )
  )

  (global \$count (ref \$i64-i64) (ref.func \$count))

  (elem declare func \$count)
  (func \$count (export "count") (param i64) (result i64)
    (if (result i64) (i64.eqz (local.get 0))
      (then (local.get 0))
      (else
        (return_call_ref \$i64-i64
          (i64.sub (local.get 0) (i64.const 1))
          (global.get \$count)
        )
      )
    )
  )

  (global \$even (ref \$i64-i64) (ref.func \$even))
  (global \$odd (ref \$i64-i64) (ref.func \$odd))

  (elem declare func \$even)
  (func \$even (export "even") (param i64) (result i64)
    (if (result i64) (i64.eqz (local.get 0))
      (then (i64.const 44))
      (else
        (return_call_ref \$i64-i64
          (i64.sub (local.get 0) (i64.const 1))
          (global.get \$odd)
        )
      )
    )
  )
  (elem declare func \$odd)
  (func \$odd (export "odd") (param i64) (result i64)
    (if (result i64) (i64.eqz (local.get 0))
      (then (i64.const 99))
      (else
        (return_call_ref \$i64-i64
          (i64.sub (local.get 0) (i64.const 1))
          (global.get \$even)
        )
      )
    )
  )
)`);

// ./test/core/return_call_ref.wast:168
assert_return(() => invoke($0, `type-i32`, []), [value("i32", 306)]);

// ./test/core/return_call_ref.wast:169
assert_return(() => invoke($0, `type-i64`, []), [value("i64", 356n)]);

// ./test/core/return_call_ref.wast:170
assert_return(() => invoke($0, `type-f32`, []), [value("f32", 3890)]);

// ./test/core/return_call_ref.wast:171
assert_return(() => invoke($0, `type-f64`, []), [value("f64", 3940)]);

// ./test/core/return_call_ref.wast:173
assert_return(() => invoke($0, `type-first-i32`, []), [value("i32", 32)]);

// ./test/core/return_call_ref.wast:174
assert_return(() => invoke($0, `type-first-i64`, []), [value("i64", 64n)]);

// ./test/core/return_call_ref.wast:175
assert_return(() => invoke($0, `type-first-f32`, []), [value("f32", 1.32)]);

// ./test/core/return_call_ref.wast:176
assert_return(() => invoke($0, `type-first-f64`, []), [value("f64", 1.64)]);

// ./test/core/return_call_ref.wast:178
assert_return(() => invoke($0, `type-second-i32`, []), [value("i32", 32)]);

// ./test/core/return_call_ref.wast:179
assert_return(() => invoke($0, `type-second-i64`, []), [value("i64", 64n)]);

// ./test/core/return_call_ref.wast:180
assert_return(() => invoke($0, `type-second-f32`, []), [value("f32", 32)]);

// ./test/core/return_call_ref.wast:181
assert_return(() => invoke($0, `type-second-f64`, []), [value("f64", 64.1)]);

// ./test/core/return_call_ref.wast:183
assert_trap(() => invoke($0, `null`, []), `null function reference`);

// ./test/core/return_call_ref.wast:185
assert_return(() => invoke($0, `fac-acc`, [0n, 1n]), [value("i64", 1n)]);

// ./test/core/return_call_ref.wast:186
assert_return(() => invoke($0, `fac-acc`, [1n, 1n]), [value("i64", 1n)]);

// ./test/core/return_call_ref.wast:187
assert_return(() => invoke($0, `fac-acc`, [5n, 1n]), [value("i64", 120n)]);

// ./test/core/return_call_ref.wast:188
assert_return(() => invoke($0, `fac-acc`, [25n, 1n]), [value("i64", 7034535277573963776n)]);

// ./test/core/return_call_ref.wast:193
assert_return(() => invoke($0, `count`, [0n]), [value("i64", 0n)]);

// ./test/core/return_call_ref.wast:194
assert_return(() => invoke($0, `count`, [1000n]), [value("i64", 0n)]);

// ./test/core/return_call_ref.wast:195
assert_return(() => invoke($0, `count`, [1000000n]), [value("i64", 0n)]);

// ./test/core/return_call_ref.wast:197
assert_return(() => invoke($0, `even`, [0n]), [value("i64", 44n)]);

// ./test/core/return_call_ref.wast:198
assert_return(() => invoke($0, `even`, [1n]), [value("i64", 99n)]);

// ./test/core/return_call_ref.wast:199
assert_return(() => invoke($0, `even`, [100n]), [value("i64", 44n)]);

// ./test/core/return_call_ref.wast:200
assert_return(() => invoke($0, `even`, [77n]), [value("i64", 99n)]);

// ./test/core/return_call_ref.wast:201
assert_return(() => invoke($0, `even`, [1000000n]), [value("i64", 44n)]);

// ./test/core/return_call_ref.wast:202
assert_return(() => invoke($0, `even`, [1000001n]), [value("i64", 99n)]);

// ./test/core/return_call_ref.wast:203
assert_return(() => invoke($0, `odd`, [0n]), [value("i64", 99n)]);

// ./test/core/return_call_ref.wast:204
assert_return(() => invoke($0, `odd`, [1n]), [value("i64", 44n)]);

// ./test/core/return_call_ref.wast:205
assert_return(() => invoke($0, `odd`, [200n]), [value("i64", 99n)]);

// ./test/core/return_call_ref.wast:206
assert_return(() => invoke($0, `odd`, [77n]), [value("i64", 44n)]);

// ./test/core/return_call_ref.wast:207
assert_return(() => invoke($0, `odd`, [1000000n]), [value("i64", 99n)]);

// ./test/core/return_call_ref.wast:208
assert_return(() => invoke($0, `odd`, [999999n]), [value("i64", 44n)]);

// ./test/core/return_call_ref.wast:213
let $1 = instantiate(`(module
  (type \$t (func))
  (type \$t1 (func (result (ref \$t))))
  (type \$t2 (func (result (ref null \$t))))
  (type \$t3 (func (result (ref func))))
  (type \$t4 (func (result (ref null func))))
  (elem declare func \$f11 \$f22 \$f33 \$f44)
  (func \$f11 (result (ref \$t)) (return_call_ref \$t1 (ref.func \$f11)))
  (func \$f21 (result (ref null \$t)) (return_call_ref \$t1 (ref.func \$f11)))
  (func \$f22 (result (ref null \$t)) (return_call_ref \$t2 (ref.func \$f22)))
  (func \$f31 (result (ref func)) (return_call_ref \$t1 (ref.func \$f11)))
  (func \$f33 (result (ref func)) (return_call_ref \$t3 (ref.func \$f33)))
  (func \$f41 (result (ref null func)) (return_call_ref \$t1 (ref.func \$f11)))
  (func \$f42 (result (ref null func)) (return_call_ref \$t2 (ref.func \$f22)))
  (func \$f43 (result (ref null func)) (return_call_ref \$t3 (ref.func \$f33)))
  (func \$f44 (result (ref null func)) (return_call_ref \$t4 (ref.func \$f44)))
)`);

// ./test/core/return_call_ref.wast:231
assert_invalid(
  () => instantiate(`(module
    (type \$t (func))
    (type \$t2 (func (result (ref null \$t))))
    (elem declare func \$f22)
    (func \$f12 (result (ref \$t)) (return_call_ref \$t2 (ref.func \$f22)))
    (func \$f22 (result (ref null \$t)) (return_call_ref \$t2 (ref.func \$f22)))
  )`),
  `type mismatch`,
);

// ./test/core/return_call_ref.wast:242
assert_invalid(
  () => instantiate(`(module
    (type \$t (func))
    (type \$t3 (func (result (ref func))))
    (elem declare func \$f33)
    (func \$f13 (result (ref \$t)) (return_call_ref \$t3 (ref.func \$f33)))
    (func \$f33 (result (ref func)) (return_call_ref \$t3 (ref.func \$f33)))
  )`),
  `type mismatch`,
);

// ./test/core/return_call_ref.wast:253
assert_invalid(
  () => instantiate(`(module
    (type \$t (func))
    (type \$t4 (func (result (ref null func))))
    (elem declare func \$f44)
    (func \$f14 (result (ref \$t)) (return_call_ref \$t4 (ref.func \$f44)))
    (func \$f44 (result (ref null func)) (return_call_ref \$t4 (ref.func \$f44)))
  )`),
  `type mismatch`,
);

// ./test/core/return_call_ref.wast:264
assert_invalid(
  () => instantiate(`(module
    (type \$t (func))
    (type \$t3 (func (result (ref func))))
    (elem declare func \$f33)
    (func \$f23 (result (ref null \$t)) (return_call_ref \$t3 (ref.func \$f33)))
    (func \$f33 (result (ref func)) (return_call_ref \$t3 (ref.func \$f33)))
  )`),
  `type mismatch`,
);

// ./test/core/return_call_ref.wast:275
assert_invalid(
  () => instantiate(`(module
    (type \$t (func))
    (type \$t4 (func (result (ref null func))))
    (elem declare func \$f44)
    (func \$f24 (result (ref null \$t)) (return_call_ref \$t4 (ref.func \$f44)))
    (func \$f44 (result (ref null func)) (return_call_ref \$t4 (ref.func \$f44)))
  )`),
  `type mismatch`,
);

// ./test/core/return_call_ref.wast:286
assert_invalid(
  () => instantiate(`(module
    (type \$t4 (func (result (ref null func))))
    (elem declare func \$f44)
    (func \$f34 (result (ref func)) (return_call_ref \$t4 (ref.func \$f44)))
    (func \$f44 (result (ref null func)) (return_call_ref \$t4 (ref.func \$f44)))
  )`),
  `type mismatch`,
);

// ./test/core/return_call_ref.wast:299
let $2 = instantiate(`(module
  (type \$t (func (result i32)))
  (func (export "unreachable") (result i32)
    (unreachable)
    (return_call_ref \$t)
  )
)`);

// ./test/core/return_call_ref.wast:306
assert_trap(() => invoke($2, `unreachable`, []), `unreachable`);

// ./test/core/return_call_ref.wast:308
let $3 = instantiate(`(module
  (elem declare func \$f)
  (type \$t (func (param i32) (result i32)))
  (func \$f (param i32) (result i32) (local.get 0))

  (func (export "unreachable") (result i32)
    (unreachable)
    (ref.func \$f)
    (return_call_ref \$t)
  )
)`);

// ./test/core/return_call_ref.wast:319
assert_trap(() => invoke($3, `unreachable`, []), `unreachable`);

// ./test/core/return_call_ref.wast:321
let $4 = instantiate(`(module
  (elem declare func \$f)
  (type \$t (func (param i32) (result i32)))
  (func \$f (param i32) (result i32) (local.get 0))

  (func (export "unreachable") (result i32)
    (unreachable)
    (i32.const 0)
    (ref.func \$f)
    (return_call_ref \$t)
    (i32.const 0)
  )
)`);

// ./test/core/return_call_ref.wast:334
assert_trap(() => invoke($4, `unreachable`, []), `unreachable`);

// ./test/core/return_call_ref.wast:336
assert_invalid(
  () => instantiate(`(module
    (elem declare func \$f)
    (type \$t (func (param i32) (result i32)))
    (func \$f (param i32) (result i32) (local.get 0))

    (func (export "unreachable") (result i32)
      (unreachable)
      (i64.const 0)
      (ref.func \$f)
      (return_call_ref \$t)
    )
  )`),
  `type mismatch`,
);

// ./test/core/return_call_ref.wast:352
assert_invalid(
  () => instantiate(`(module
    (elem declare func \$f)
    (type \$t (func (param i32) (result i32)))
    (func \$f (param i32) (result i32) (local.get 0))

    (func (export "unreachable") (result i32)
      (unreachable)
      (ref.func \$f)
      (return_call_ref \$t)
      (i64.const 0)
    )
  )`),
  `type mismatch`,
);

// ./test/core/return_call_ref.wast:368
assert_invalid(
  () => instantiate(`(module
    (type \$t (func))
    (func \$f (param \$r externref)
      (return_call_ref \$t (local.get \$r))
    )
  )`),
  `type mismatch`,
);

// ./test/core/return_call_ref.wast:378
assert_invalid(
  () => instantiate(`(module
    (type \$t (func))
    (func \$f (param \$r funcref)
      (return_call_ref \$t (local.get \$r))
    )
  )`),
  `type mismatch`,
);

// ./test/core/return_call_ref.wast:388
assert_invalid(
  () => instantiate(`(module
    (type \$ty (func (result i32 i32)))
    (func (param (ref \$ty)) (result i32)
      local.get 0
      return_call_ref \$ty
    )
  )`),
  `type mismatch`,
);
