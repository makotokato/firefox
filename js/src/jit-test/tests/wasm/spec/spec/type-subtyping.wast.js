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

// ./test/core/gc/type-subtyping.wast

// ./test/core/gc/type-subtyping.wast:3
let $0 = instantiate(`(module
  (type \$e0 (sub (array i32)))
  (type \$e1 (sub \$e0 (array i32)))

  (type \$e2 (sub (array anyref)))
  (type \$e3 (sub (array (ref null \$e0))))
  (type \$e4 (sub (array (ref \$e1))))

  (type \$m1 (sub (array (mut i32))))
  (type \$m2 (sub \$m1 (array (mut i32))))
)`);

// ./test/core/gc/type-subtyping.wast:15
let $1 = instantiate(`(module
  (type \$e0 (sub (struct)))
  (type \$e1 (sub \$e0 (struct)))
  (type \$e2 (sub \$e1 (struct (field i32))))
  (type \$e3 (sub \$e2 (struct (field i32 (ref null \$e0)))))
  (type \$e4 (sub \$e3 (struct (field i32 (ref \$e0) (mut i64)))))
  (type \$e5 (sub \$e4 (struct (field i32 (ref \$e1) (mut i64)))))
)`);

// ./test/core/gc/type-subtyping.wast:24
let $2 = instantiate(`(module
  (type \$s (sub (struct)))
  (type \$s' (sub \$s (struct)))

  (type \$f1 (sub (func (param (ref \$s')) (result anyref))))
  (type \$f2 (sub \$f1 (func (param (ref \$s)) (result (ref any)))))
  (type \$f3 (sub \$f2 (func (param (ref null \$s)) (result (ref \$s)))))
  (type \$f4 (sub \$f3 (func (param (ref null struct)) (result (ref \$s')))))
)`);

// ./test/core/gc/type-subtyping.wast:37
let $3 = instantiate(`(module
  (type \$t (sub (struct (field anyref))))
  (rec (type \$r (sub \$t (struct (field (ref \$r))))))
  (type \$t' (sub \$r (struct (field (ref \$r) i32))))
)`);

// ./test/core/gc/type-subtyping.wast:43
let $4 = instantiate(`(module
  (rec
    (type \$r1 (sub (struct (field i32 (ref \$r1)))))
  )
  (rec
    (type \$r2 (sub \$r1 (struct (field i32 (ref \$r3)))))
    (type \$r3 (sub \$r1 (struct (field i32 (ref \$r2)))))
  )
)`);

// ./test/core/gc/type-subtyping.wast:53
let $5 = instantiate(`(module
  (rec
    (type \$a1 (sub (struct (field i32 (ref \$a2)))))
    (type \$a2 (sub (struct (field i64 (ref \$a1)))))
  )
  (rec
    (type \$b1 (sub \$a2 (struct (field i64 (ref \$a1) i32))))
    (type \$b2 (sub \$a1 (struct (field i32 (ref \$a2) i32))))
    (type \$b3 (sub \$a2 (struct (field i64 (ref \$b2) i32))))
  )
)`);

// ./test/core/gc/type-subtyping.wast:68
let $6 = instantiate(`(module
  (rec
    (type \$t1 (sub (func (param i32 (ref \$t3)))))
    (type \$t2 (sub \$t1 (func (param i32 (ref \$t2)))))
    (type \$t3 (sub \$t2 (func (param i32 (ref \$t1)))))
  )

  (func \$f1 (param \$r (ref \$t1))
    (call \$f1 (local.get \$r))
  )
  (func \$f2 (param \$r (ref \$t2))
    (call \$f1 (local.get \$r))
    (call \$f2 (local.get \$r))
  )
  (func \$f3 (param \$r (ref \$t3))
    (call \$f1 (local.get \$r))
    (call \$f2 (local.get \$r))
    (call \$f3 (local.get \$r))
  )
)`);

// ./test/core/gc/type-subtyping.wast:89
let $7 = instantiate(`(module
  (rec
    (type \$t1 (sub (func (result i32 (ref \$u1)))))
    (type \$u1 (sub (func (result f32 (ref \$t1)))))
  )

  (rec
    (type \$t2 (sub \$t1 (func (result i32 (ref \$u3)))))
    (type \$u2 (sub \$u1 (func (result f32 (ref \$t3)))))
    (type \$t3 (sub \$t1 (func (result i32 (ref \$u2)))))
    (type \$u3 (sub \$u1 (func (result f32 (ref \$t2)))))
  )

  (func \$f1 (param \$r (ref \$t1))
    (call \$f1 (local.get \$r))
  )
  (func \$f2 (param \$r (ref \$t2))
    (call \$f1 (local.get \$r))
    (call \$f2 (local.get \$r))
  )
  (func \$f3 (param \$r (ref \$t3))
    (call \$f1 (local.get \$r))
    (call \$f3 (local.get \$r))
  )
)`);

// ./test/core/gc/type-subtyping.wast:115
let $8 = instantiate(`(module
  (rec (type \$f1 (sub (func))) (type (struct (field (ref \$f1)))))
  (rec (type \$f2 (sub (func))) (type (struct (field (ref \$f2)))))
  (rec (type \$g1 (sub \$f1 (func))) (type (struct)))
  (rec (type \$g2 (sub \$f2 (func))) (type (struct)))
  (func \$g (type \$g2))
  (global (ref \$g1) (ref.func \$g))
)`);

// ./test/core/gc/type-subtyping.wast:124
let $9 = instantiate(`(module
  (rec (type \$f1 (sub (func))) (type \$s1 (sub (struct (field (ref \$f1))))))
  (rec (type \$f2 (sub (func))) (type \$s2 (sub (struct (field (ref \$f2))))))
  (rec
    (type \$g1 (sub \$f1 (func)))
    (type (sub \$s1 (struct (field (ref \$f1) (ref \$f1) (ref \$f2) (ref \$f2) (ref \$g1)))))
  )
  (rec
    (type \$g2 (sub \$f2 (func)))
    (type (sub \$s2 (struct (field (ref \$f1) (ref \$f2) (ref \$f1) (ref \$f2) (ref \$g2)))))
  )
  (func \$g (type \$g2))
  (global (ref \$g1) (ref.func \$g))
)`);

// ./test/core/gc/type-subtyping.wast:139
assert_invalid(
  () => instantiate(`(module
    (rec (type \$f1 (sub (func))) (type (struct (field (ref \$f1)))))
    (rec (type \$f2 (sub (func))) (type (struct (field (ref \$f1)))))
    (rec (type \$g1 (sub \$f1 (func))) (type (struct)))
    (rec (type \$g2 (sub \$f2 (func))) (type (struct)))
    (func \$g (type \$g2))
    (global (ref \$g1) (ref.func \$g))
  )`),
  `type mismatch`,
);

// ./test/core/gc/type-subtyping.wast:151
let $10 = instantiate(`(module
  (rec (type \$f1 (sub (func))) (type (struct (field (ref \$f1)))))
  (rec (type \$f2 (sub (func))) (type (struct (field (ref \$f2)))))
  (rec (type \$g (sub \$f1 (func))) (type (struct)))
  (func \$g (type \$g))
  (global (ref \$f1) (ref.func \$g))
)`);

// ./test/core/gc/type-subtyping.wast:159
let $11 = instantiate(`(module
  (rec (type \$f1 (sub (func))) (type \$s1 (sub (struct (field (ref \$f1))))))
  (rec (type \$f2 (sub (func))) (type \$s2 (sub (struct (field (ref \$f2))))))
  (rec
    (type \$g1 (sub \$f1 (func)))
    (type (sub \$s1 (struct (field (ref \$f1) (ref \$f1) (ref \$f2) (ref \$f2) (ref \$g1)))))
  )
  (rec
    (type \$g2 (sub \$f2 (func)))
    (type (sub \$s2 (struct (field (ref \$f1) (ref \$f2) (ref \$f1) (ref \$f2) (ref \$g2)))))
  )
  (rec (type \$h (sub \$g2 (func))) (type (struct)))
  (func \$h (type \$h))
  (global (ref \$f1) (ref.func \$h))
  (global (ref \$g1) (ref.func \$h))
)`);

// ./test/core/gc/type-subtyping.wast:177
let $12 = instantiate(`(module
  (rec (type \$f11 (sub (func (result (ref func))))) (type \$f12 (sub \$f11 (func (result (ref \$f11))))))
  (rec (type \$f21 (sub (func (result (ref func))))) (type \$f22 (sub \$f21 (func (result (ref \$f21))))))
  (func \$f11 (type \$f11) (unreachable))
  (func \$f12 (type \$f12) (unreachable))
  (global (ref \$f11) (ref.func \$f11))
  (global (ref \$f21) (ref.func \$f11))
  (global (ref \$f12) (ref.func \$f12))
  (global (ref \$f22) (ref.func \$f12))
)`);

// ./test/core/gc/type-subtyping.wast:188
let $13 = instantiate(`(module
  (rec (type \$f11 (sub (func (result (ref func))))) (type \$f12 (sub \$f11 (func (result (ref \$f11))))))
  (rec (type \$f21 (sub (func (result (ref func))))) (type \$f22 (sub \$f21 (func (result (ref \$f21))))))
  (rec (type \$g11 (sub \$f11 (func (result (ref func))))) (type \$g12 (sub \$g11 (func (result (ref \$g11))))))
  (rec (type \$g21 (sub \$f21 (func (result (ref func))))) (type \$g22 (sub \$g21 (func (result (ref \$g21))))))
  (func \$g11 (type \$g11) (unreachable))
  (func \$g12 (type \$g12) (unreachable))
  (global (ref \$f11) (ref.func \$g11))
  (global (ref \$f21) (ref.func \$g11))
  (global (ref \$f11) (ref.func \$g12))
  (global (ref \$f21) (ref.func \$g12))
  (global (ref \$g11) (ref.func \$g11))
  (global (ref \$g21) (ref.func \$g11))
  (global (ref \$g12) (ref.func \$g12))
  (global (ref \$g22) (ref.func \$g12))
)`);

// ./test/core/gc/type-subtyping.wast:205
assert_invalid(
  () => instantiate(`(module
    (rec (type \$f11 (sub (func))) (type \$f12 (sub \$f11 (func))))
    (rec (type \$f21 (sub (func))) (type \$f22 (sub \$f11 (func))))
    (func \$f (type \$f21))
    (global (ref \$f11) (ref.func \$f))
  )`),
  `type mismatch`,
);

// ./test/core/gc/type-subtyping.wast:215
assert_invalid(
  () => instantiate(`(module
    (rec (type \$f01 (sub (func))) (type \$f02 (sub \$f01 (func))))
    (rec (type \$f11 (sub (func))) (type \$f12 (sub \$f01 (func))))
    (rec (type \$f21 (sub (func))) (type \$f22 (sub \$f11 (func))))
    (func \$f (type \$f21))
    (global (ref \$f11) (ref.func \$f))
  )`),
  `type mismatch`,
);

// ./test/core/gc/type-subtyping.wast:229
let $14 = instantiate(`(module
  (type \$t0 (sub (func (result (ref null func)))))
  (rec (type \$t1 (sub \$t0 (func (result (ref null \$t1))))))
  (rec (type \$t2 (sub \$t1 (func (result (ref null \$t2))))))

  (func \$f0 (type \$t0) (ref.null func))
  (func \$f1 (type \$t1) (ref.null \$t1))
  (func \$f2 (type \$t2) (ref.null \$t2))
  (table funcref (elem \$f0 \$f1 \$f2))

  (func (export "run")
    (block (result (ref null func)) (call_indirect (type \$t0) (i32.const 0)))
    (block (result (ref null func)) (call_indirect (type \$t0) (i32.const 1)))
    (block (result (ref null func)) (call_indirect (type \$t0) (i32.const 2)))
    (block (result (ref null \$t1)) (call_indirect (type \$t1) (i32.const 1)))
    (block (result (ref null \$t1)) (call_indirect (type \$t1) (i32.const 2)))
    (block (result (ref null \$t2)) (call_indirect (type \$t2) (i32.const 2)))

    (block (result (ref null \$t0)) (ref.cast (ref \$t0) (table.get (i32.const 0))))
    (block (result (ref null \$t0)) (ref.cast (ref \$t0) (table.get (i32.const 1))))
    (block (result (ref null \$t0)) (ref.cast (ref \$t0) (table.get (i32.const 2))))
    (block (result (ref null \$t1)) (ref.cast (ref \$t1) (table.get (i32.const 1))))
    (block (result (ref null \$t1)) (ref.cast (ref \$t1) (table.get (i32.const 2))))
    (block (result (ref null \$t2)) (ref.cast (ref \$t2) (table.get (i32.const 2))))
    (br 0)
  )

  (func (export "fail1")
    (block (result (ref null \$t1)) (call_indirect (type \$t1) (i32.const 0)))
    (br 0)
  )
  (func (export "fail2")
    (block (result (ref null \$t1)) (call_indirect (type \$t2) (i32.const 0)))
    (br 0)
  )
  (func (export "fail3")
    (block (result (ref null \$t1)) (call_indirect (type \$t2) (i32.const 1)))
    (br 0)
  )

  (func (export "fail4")
    (ref.cast (ref \$t1) (table.get (i32.const 0)))
    (br 0)
  )
  (func (export "fail5")
    (ref.cast (ref \$t2) (table.get (i32.const 0)))
    (br 0)
  )
  (func (export "fail6")
    (ref.cast (ref \$t2) (table.get (i32.const 1)))
    (br 0)
  )
)`);

// ./test/core/gc/type-subtyping.wast:282
assert_return(() => invoke($14, `run`, []), []);

// ./test/core/gc/type-subtyping.wast:283
assert_trap(() => invoke($14, `fail1`, []), `indirect call`);

// ./test/core/gc/type-subtyping.wast:284
assert_trap(() => invoke($14, `fail2`, []), `indirect call`);

// ./test/core/gc/type-subtyping.wast:285
assert_trap(() => invoke($14, `fail3`, []), `indirect call`);

// ./test/core/gc/type-subtyping.wast:286
assert_trap(() => invoke($14, `fail4`, []), `cast`);

// ./test/core/gc/type-subtyping.wast:287
assert_trap(() => invoke($14, `fail5`, []), `cast`);

// ./test/core/gc/type-subtyping.wast:288
assert_trap(() => invoke($14, `fail6`, []), `cast`);

// ./test/core/gc/type-subtyping.wast:290
let $15 = instantiate(`(module
  (type \$t1 (sub (func)))
  (type \$t2 (sub final (func)))

  (func \$f1 (type \$t1))
  (func \$f2 (type \$t2))
  (table funcref (elem \$f1 \$f2))

  (func (export "fail1")
    (block (call_indirect (type \$t1) (i32.const 1)))
  )
  (func (export "fail2")
    (block (call_indirect (type \$t2) (i32.const 0)))
  )

  (func (export "fail3")
    (ref.cast (ref \$t1) (table.get (i32.const 1)))
    (drop)
  )
  (func (export "fail4")
    (ref.cast (ref \$t2) (table.get (i32.const 0)))
    (drop)
  )
)`);

// ./test/core/gc/type-subtyping.wast:314
assert_trap(() => invoke($15, `fail1`, []), `indirect call`);

// ./test/core/gc/type-subtyping.wast:315
assert_trap(() => invoke($15, `fail2`, []), `indirect call`);

// ./test/core/gc/type-subtyping.wast:316
assert_trap(() => invoke($15, `fail3`, []), `cast`);

// ./test/core/gc/type-subtyping.wast:317
assert_trap(() => invoke($15, `fail4`, []), `cast`);

// ./test/core/gc/type-subtyping.wast:319
let $16 = instantiate(`(module
  (type \$t1 (sub (func)))
  (type \$t2 (sub \$t1 (func)))
  (type \$t3 (sub \$t2 (func)))
  (type \$t4 (sub final (func)))

  (func \$f2 (type \$t2))
  (func \$f3 (type \$t3))
  (table (ref null \$t2) (elem \$f2 \$f3))

  (func (export "run")
    (call_indirect (type \$t1) (i32.const 0))
    (call_indirect (type \$t1) (i32.const 1))
    (call_indirect (type \$t2) (i32.const 0))
    (call_indirect (type \$t2) (i32.const 1))
    (call_indirect (type \$t3) (i32.const 1))
  )

  (func (export "fail1")
    (call_indirect (type \$t3) (i32.const 0))
  )
  (func (export "fail2")
    (call_indirect (type \$t4) (i32.const 0))
  )
)`);

// ./test/core/gc/type-subtyping.wast:344
assert_return(() => invoke($16, `run`, []), []);

// ./test/core/gc/type-subtyping.wast:345
assert_trap(() => invoke($16, `fail1`, []), `indirect call`);

// ./test/core/gc/type-subtyping.wast:346
assert_trap(() => invoke($16, `fail2`, []), `indirect call`);

// ./test/core/gc/type-subtyping.wast:348
let $17 = instantiate(`(module
  (rec (type \$f1 (sub (func))) (type (struct (field (ref \$f1)))))
  (rec (type \$f2 (sub (func))) (type (struct (field (ref \$f2)))))
  (rec (type \$g1 (sub \$f1 (func))) (type (struct)))
  (rec (type \$g2 (sub \$f2 (func))) (type (struct)))
  (func \$g (type \$g2)) (elem declare func \$g)
  (func (export "run") (result i32)
    (ref.test (ref \$g1) (ref.func \$g))
  )
)`);

// ./test/core/gc/type-subtyping.wast:358
assert_return(() => invoke($17, `run`, []), [value("i32", 1)]);

// ./test/core/gc/type-subtyping.wast:360
let $18 = instantiate(`(module
  (rec (type \$f1 (sub (func))) (type \$s1 (sub (struct (field (ref \$f1))))))
  (rec (type \$f2 (sub (func))) (type \$s2 (sub (struct (field (ref \$f2))))))
  (rec
    (type \$g1 (sub \$f1 (func)))
    (type (sub \$s1 (struct (field (ref \$f1) (ref \$f1) (ref \$f2) (ref \$f2) (ref \$g1)))))
  )
  (rec
    (type \$g2 (sub \$f2 (func)))
    (type (sub \$s2 (struct (field (ref \$f1) (ref \$f2) (ref \$f1) (ref \$f2) (ref \$g2)))))
  )
  (func \$g (type \$g2)) (elem declare func \$g)
  (func (export "run") (result i32)
    (ref.test (ref \$g1) (ref.func \$g))
  )
)`);

// ./test/core/gc/type-subtyping.wast:376
assert_return(() => invoke($18, `run`, []), [value("i32", 1)]);

// ./test/core/gc/type-subtyping.wast:378
let $19 = instantiate(`(module
  (rec (type \$f1 (sub (func))) (type (struct (field (ref \$f1)))))
  (rec (type \$f2 (sub (func))) (type (struct (field (ref \$f1)))))
  (rec (type \$g1 (sub \$f1 (func))) (type (struct)))
  (rec (type \$g2 (sub \$f2 (func))) (type (struct)))
  (func \$g (type \$g2)) (elem declare func \$g)
  (func (export "run") (result i32)
    (ref.test (ref \$g1) (ref.func \$g))
  )
)`);

// ./test/core/gc/type-subtyping.wast:388
assert_return(() => invoke($19, `run`, []), [value("i32", 0)]);

// ./test/core/gc/type-subtyping.wast:390
let $20 = instantiate(`(module
  (rec (type \$f1 (sub (func))) (type (struct (field (ref \$f1)))))
  (rec (type \$f2 (sub (func))) (type (struct (field (ref \$f2)))))
  (rec (type \$g (sub \$f1 (func))) (type (struct)))
  (func \$g (type \$g)) (elem declare func \$g)
  (func (export "run") (result i32)
    (ref.test (ref \$f1) (ref.func \$g))
  )
)`);

// ./test/core/gc/type-subtyping.wast:399
assert_return(() => invoke($20, `run`, []), [value("i32", 1)]);

// ./test/core/gc/type-subtyping.wast:401
let $21 = instantiate(`(module
  (rec (type \$f1 (sub (func))) (type \$s1 (sub (struct (field (ref \$f1))))))
  (rec (type \$f2 (sub (func))) (type \$s2 (sub (struct (field (ref \$f2))))))
  (rec
    (type \$g1 (sub \$f1 (func)))
    (type (sub \$s1 (struct (field (ref \$f1) (ref \$f1) (ref \$f2) (ref \$f2) (ref \$g1)))))
  )
  (rec
    (type \$g2 (sub \$f2 (func)))
    (type (sub \$s2 (struct (field (ref \$f1) (ref \$f2) (ref \$f1) (ref \$f2) (ref \$g2)))))
  )
  (rec (type \$h (sub \$g2 (func))) (type (struct)))
  (func \$h (type \$h)) (elem declare func \$h)
  (func (export "run") (result i32 i32)
    (ref.test (ref \$f1) (ref.func \$h))
    (ref.test (ref \$g1) (ref.func \$h))
  )
)`);

// ./test/core/gc/type-subtyping.wast:419
assert_return(() => invoke($21, `run`, []), [value("i32", 1), value("i32", 1)]);

// ./test/core/gc/type-subtyping.wast:422
let $22 = instantiate(`(module
  (rec (type \$f11 (sub (func (result (ref func))))) (type \$f12 (sub \$f11 (func (result (ref \$f11))))))
  (rec (type \$f21 (sub (func (result (ref func))))) (type \$f22 (sub \$f21 (func (result (ref \$f21))))))
  (func \$f11 (type \$f11) (unreachable)) (elem declare func \$f11)
  (func \$f12 (type \$f12) (unreachable)) (elem declare func \$f12)
  (func (export "run") (result i32 i32 i32 i32)
    (ref.test (ref \$f11) (ref.func \$f11))
    (ref.test (ref \$f21) (ref.func \$f11))
    (ref.test (ref \$f12) (ref.func \$f12))
    (ref.test (ref \$f22) (ref.func \$f12))
  )
)`);

// ./test/core/gc/type-subtyping.wast:434
assert_return(
  () => invoke($22, `run`, []),
  [value("i32", 1), value("i32", 1), value("i32", 1), value("i32", 1)],
);

// ./test/core/gc/type-subtyping.wast:438
let $23 = instantiate(`(module
  (rec (type \$f11 (sub (func (result (ref func))))) (type \$f12 (sub \$f11 (func (result (ref \$f11))))))
  (rec (type \$f21 (sub (func (result (ref func))))) (type \$f22 (sub \$f21 (func (result (ref \$f21))))))
  (rec (type \$g11 (sub \$f11 (func (result (ref func))))) (type \$g12 (sub \$g11 (func (result (ref \$g11))))))
  (rec (type \$g21 (sub \$f21 (func (result (ref func))))) (type \$g22 (sub \$g21 (func (result (ref \$g21))))))
  (func \$g11 (type \$g11) (unreachable)) (elem declare func \$g11)
  (func \$g12 (type \$g12) (unreachable)) (elem declare func \$g12)
  (func (export "run") (result i32 i32 i32 i32 i32 i32 i32 i32)
    (ref.test (ref \$f11) (ref.func \$g11))
    (ref.test (ref \$f21) (ref.func \$g11))
    (ref.test (ref \$f11) (ref.func \$g12))
    (ref.test (ref \$f21) (ref.func \$g12))
    (ref.test (ref \$g11) (ref.func \$g11))
    (ref.test (ref \$g21) (ref.func \$g11))
    (ref.test (ref \$g12) (ref.func \$g12))
    (ref.test (ref \$g22) (ref.func \$g12))
  )
)`);

// ./test/core/gc/type-subtyping.wast:456
assert_return(
  () => invoke($23, `run`, []),
  [
    value("i32", 1),
    value("i32", 1),
    value("i32", 1),
    value("i32", 1),
    value("i32", 1),
    value("i32", 1),
    value("i32", 1),
    value("i32", 1),
  ],
);

// ./test/core/gc/type-subtyping.wast:461
let $24 = instantiate(`(module
  (rec (type \$f11 (sub (func))) (type \$f12 (sub \$f11 (func))))
  (rec (type \$f21 (sub (func))) (type \$f22 (sub \$f11 (func))))
  (func \$f (type \$f21)) (elem declare func \$f)
  (func (export "run") (result i32)
    (ref.test (ref \$f11) (ref.func \$f))
  )
)`);

// ./test/core/gc/type-subtyping.wast:469
assert_return(() => invoke($24, `run`, []), [value("i32", 0)]);

// ./test/core/gc/type-subtyping.wast:471
let $25 = instantiate(`(module
  (rec (type \$f01 (sub (func))) (type \$f02 (sub \$f01 (func))))
  (rec (type \$f11 (sub (func))) (type \$f12 (sub \$f01 (func))))
  (rec (type \$f21 (sub (func))) (type \$f22 (sub \$f11 (func))))
  (func \$f (type \$f21)) (elem declare func \$f)
  (func (export "run") (result i32)
    (ref.test (ref \$f11) (ref.func \$f))
  )
)`);

// ./test/core/gc/type-subtyping.wast:480
assert_return(() => invoke($25, `run`, []), [value("i32", 0)]);

// ./test/core/gc/type-subtyping.wast:486
let $26 = instantiate(`(module
  (type \$t0 (sub (func (result (ref null func)))))
  (rec (type \$t1 (sub \$t0 (func (result (ref null \$t1))))))
  (rec (type \$t2 (sub \$t1 (func (result (ref null \$t2))))))

  (func (export "f0") (type \$t0) (ref.null func))
  (func (export "f1") (type \$t1) (ref.null \$t1))
  (func (export "f2") (type \$t2) (ref.null \$t2))
)`);

// ./test/core/gc/type-subtyping.wast:495
register($26, `M`);

// ./test/core/gc/type-subtyping.wast:497
let $27 = instantiate(`(module
  (type \$t0 (sub (func (result (ref null func)))))
  (rec (type \$t1 (sub \$t0 (func (result (ref null \$t1))))))
  (rec (type \$t2 (sub \$t1 (func (result (ref null \$t2))))))

  (func (import "M" "f0") (type \$t0))
  (func (import "M" "f1") (type \$t0))
  (func (import "M" "f1") (type \$t1))
  (func (import "M" "f2") (type \$t0))
  (func (import "M" "f2") (type \$t1))
  (func (import "M" "f2") (type \$t2))
)`);

// ./test/core/gc/type-subtyping.wast:510
assert_unlinkable(
  () => instantiate(`(module
    (type \$t0 (sub (func (result (ref null func)))))
    (rec (type \$t1 (sub \$t0 (func (result (ref null \$t1))))))
    (rec (type \$t2 (sub \$t1 (func (result (ref null \$t2))))))
    (func (import "M" "f0") (type \$t1))
  )`),
  `incompatible import type`,
);

// ./test/core/gc/type-subtyping.wast:520
assert_unlinkable(
  () => instantiate(`(module
    (type \$t0 (sub (func (result (ref null func)))))
    (rec (type \$t1 (sub \$t0 (func (result (ref null \$t1))))))
    (rec (type \$t2 (sub \$t1 (func (result (ref null \$t2))))))
    (func (import "M" "f0") (type \$t2))
  )`),
  `incompatible import type`,
);

// ./test/core/gc/type-subtyping.wast:530
assert_unlinkable(
  () => instantiate(`(module
    (type \$t0 (sub (func (result (ref null func)))))
    (rec (type \$t1 (sub \$t0 (func (result (ref null \$t1))))))
    (rec (type \$t2 (sub \$t1 (func (result (ref null \$t2))))))
    (func (import "M" "f1") (type \$t2))
  )`),
  `incompatible import type`,
);

// ./test/core/gc/type-subtyping.wast:540
let $28 = instantiate(`(module
  (type \$t1 (sub (func)))
  (type \$t2 (sub final (func)))
  (func (export "f1") (type \$t1))
  (func (export "f2") (type \$t2))
)`);

// ./test/core/gc/type-subtyping.wast:546
register($28, `M2`);

// ./test/core/gc/type-subtyping.wast:548
assert_unlinkable(
  () => instantiate(`(module
    (type \$t1 (sub (func)))
    (type \$t2 (sub final (func)))
    (func (import "M2" "f1") (type \$t2))
  )`),
  `incompatible import type`,
);

// ./test/core/gc/type-subtyping.wast:556
assert_unlinkable(
  () => instantiate(`(module
    (type \$t1 (sub (func)))
    (type \$t2 (sub final (func)))
    (func (import "M2" "f2") (type \$t1))
  )`),
  `incompatible import type`,
);

// ./test/core/gc/type-subtyping.wast:566
let $29 = instantiate(`(module
  (rec (type \$f2 (sub (func))) (type (struct (field (ref \$f2)))))
  (rec (type \$g2 (sub \$f2 (func))) (type (struct)))
  (func (export "g") (type \$g2))
)`);

// ./test/core/gc/type-subtyping.wast:571
register($29, `M3`);

// ./test/core/gc/type-subtyping.wast:572
let $30 = instantiate(`(module
  (rec (type \$f1 (sub (func))) (type (struct (field (ref \$f1)))))
  (rec (type \$g1 (sub \$f1 (func))) (type (struct)))
  (func (import "M3" "g") (type \$g1))
)`);

// ./test/core/gc/type-subtyping.wast:578
let $31 = instantiate(`(module
  (rec (type \$f1 (sub (func))) (type \$s1 (sub (struct (field (ref \$f1))))))
  (rec (type \$f2 (sub (func))) (type \$s2 (sub (struct (field (ref \$f2))))))
  (rec
    (type \$g2 (sub \$f2 (func)))
    (type (sub \$s2 (struct (field (ref \$f1) (ref \$f2) (ref \$f1) (ref \$f2) (ref \$g2)))))
  )
  (func (export "g") (type \$g2))
)`);

// ./test/core/gc/type-subtyping.wast:587
register($31, `M4`);

// ./test/core/gc/type-subtyping.wast:588
let $32 = instantiate(`(module
  (rec (type \$f1 (sub (func))) (type \$s1 (sub (struct (field (ref \$f1))))))
  (rec (type \$f2 (sub (func))) (type \$s2 (sub (struct (field (ref \$f2))))))
  (rec
    (type \$g1 (sub \$f1 (func)))
    (type (sub \$s1 (struct (field (ref \$f1) (ref \$f1) (ref \$f2) (ref \$f2) (ref \$g1)))))
  )
  (func (import "M4" "g") (type \$g1))
)`);

// ./test/core/gc/type-subtyping.wast:598
let $33 = instantiate(`(module
  (rec (type \$f1 (sub (func))) (type (struct (field (ref \$f1)))))
  (rec (type \$f2 (sub (func))) (type (struct (field (ref \$f1)))))
  (rec (type \$g2 (sub \$f2 (func))) (type (struct)))
  (func (export "g") (type \$g2))
)`);

// ./test/core/gc/type-subtyping.wast:604
register($33, `M5`);

// ./test/core/gc/type-subtyping.wast:605
assert_unlinkable(
  () => instantiate(`(module
    (rec (type \$f1 (sub (func))) (type (struct (field (ref \$f1)))))
    (rec (type \$g1 (sub \$f1 (func))) (type (struct)))
    (func (import "M5" "g") (type \$g1))
  )`),
  `incompatible import`,
);

// ./test/core/gc/type-subtyping.wast:614
let $34 = instantiate(`(module
  (rec (type \$f1 (sub (func))) (type (struct (field (ref \$f1)))))
  (rec (type \$f2 (sub (func))) (type (struct (field (ref \$f2)))))
  (rec (type \$g (sub \$f1 (func))) (type (struct)))
  (func (export "g") (type \$g))
)`);

// ./test/core/gc/type-subtyping.wast:620
register($34, `M6`);

// ./test/core/gc/type-subtyping.wast:621
let $35 = instantiate(`(module
  (rec (type \$f1 (sub (func))) (type (struct (field (ref \$f1)))))
  (rec (type \$f2 (sub (func))) (type (struct (field (ref \$f2)))))
  (rec (type \$g (sub \$f1 (func))) (type (struct)))
  (func (import "M6" "g") (type \$f1))
)`);

// ./test/core/gc/type-subtyping.wast:628
let $36 = instantiate(`(module
  (rec (type \$f1 (sub (func))) (type \$s1 (sub (struct (field (ref \$f1))))))
  (rec (type \$f2 (sub (func))) (type \$s2 (sub (struct (field (ref \$f2))))))
  (rec
    (type \$g2 (sub \$f2 (func)))
    (type (sub \$s2 (struct (field (ref \$f1) (ref \$f2) (ref \$f1) (ref \$f2) (ref \$g2)))))
  )
  (rec (type \$h (sub \$g2 (func))) (type (struct)))
  (func (export "h") (type \$h))
)`);

// ./test/core/gc/type-subtyping.wast:638
register($36, `M7`);

// ./test/core/gc/type-subtyping.wast:639
let $37 = instantiate(`(module
  (rec (type \$f1 (sub (func))) (type \$s1 (sub (struct (field (ref \$f1))))))
  (rec (type \$f2 (sub (func))) (type \$s2 (sub (struct (field (ref \$f2))))))
  (rec
    (type \$g1 (sub \$f1 (func)))
    (type (sub \$s1 (struct (field (ref \$f1) (ref \$f1) (ref \$f2) (ref \$f2) (ref \$g1)))))
  )
  (rec (type \$h (sub \$g1 (func))) (type (struct)))
  (func (import "M7" "h") (type \$f1))
  (func (import "M7" "h") (type \$g1))
)`);

// ./test/core/gc/type-subtyping.wast:652
let $38 = instantiate(`(module
  (rec (type \$f11 (sub (func (result (ref func))))) (type \$f12 (sub \$f11 (func (result (ref \$f11))))))
  (rec (type \$f21 (sub (func (result (ref func))))) (type \$f22 (sub \$f21 (func (result (ref \$f21))))))
  (func (export "f11") (type \$f11) (unreachable))
  (func (export "f12") (type \$f12) (unreachable))
)`);

// ./test/core/gc/type-subtyping.wast:658
register($38, `M8`);

// ./test/core/gc/type-subtyping.wast:659
let $39 = instantiate(`(module
  (rec (type \$f11 (sub (func (result (ref func))))) (type \$f12 (sub \$f11 (func (result (ref \$f11))))))
  (rec (type \$f21 (sub (func (result (ref func))))) (type \$f22 (sub \$f21 (func (result (ref \$f21))))))
  (func (import "M8" "f11") (type \$f11))
  (func (import "M8" "f11") (type \$f21))
  (func (import "M8" "f12") (type \$f12))
  (func (import "M8" "f12") (type \$f22))
)`);

// ./test/core/gc/type-subtyping.wast:668
let $40 = instantiate(`(module
  (rec (type \$f11 (sub (func (result (ref func))))) (type \$f12 (sub \$f11 (func (result (ref \$f11))))))
  (rec (type \$f21 (sub (func (result (ref func))))) (type \$f22 (sub \$f21 (func (result (ref \$f21))))))
  (rec (type \$g11 (sub \$f11 (func (result (ref func))))) (type \$g12 (sub \$g11 (func (result (ref \$g11))))))
  (rec (type \$g21 (sub \$f21 (func (result (ref func))))) (type \$g22 (sub \$g21 (func (result (ref \$g21))))))
  (func (export "g11") (type \$g11) (unreachable))
  (func (export "g12") (type \$g12) (unreachable))
)`);

// ./test/core/gc/type-subtyping.wast:676
register($40, `M9`);

// ./test/core/gc/type-subtyping.wast:677
let $41 = instantiate(`(module
  (rec (type \$f11 (sub (func (result (ref func))))) (type \$f12 (sub \$f11 (func (result (ref \$f11))))))
  (rec (type \$f21 (sub (func (result (ref func))))) (type \$f22 (sub \$f21 (func (result (ref \$f21))))))
  (rec (type \$g11 (sub \$f11 (func (result (ref func))))) (type \$g12 (sub \$g11 (func (result (ref \$g11))))))
  (rec (type \$g21 (sub \$f21 (func (result (ref func))))) (type \$g22 (sub \$g21 (func (result (ref \$g21))))))
  (func (import "M9" "g11") (type \$f11))
  (func (import "M9" "g11") (type \$f21))
  (func (import "M9" "g12") (type \$f11))
  (func (import "M9" "g12") (type \$f21))
  (func (import "M9" "g11") (type \$g11))
  (func (import "M9" "g11") (type \$g21))
  (func (import "M9" "g12") (type \$g12))
  (func (import "M9" "g12") (type \$g22))
)`);

// ./test/core/gc/type-subtyping.wast:692
let $42 = instantiate(`(module
  (rec (type \$f11 (sub (func))) (type \$f12 (sub \$f11 (func))))
  (rec (type \$f21 (sub (func))) (type \$f22 (sub \$f11 (func))))
  (func (export "f") (type \$f21))
)`);

// ./test/core/gc/type-subtyping.wast:697
register($42, `M10`);

// ./test/core/gc/type-subtyping.wast:698
assert_unlinkable(
  () => instantiate(`(module
    (rec (type \$f11 (sub (func))) (type \$f12 (sub \$f11 (func))))
    (func (import "M10" "f") (type \$f11))
  )`),
  `incompatible import`,
);

// ./test/core/gc/type-subtyping.wast:706
let $43 = instantiate(`(module
  (rec (type \$f01 (sub (func))) (type \$f02 (sub \$f01 (func))))
  (rec (type \$f11 (sub (func))) (type \$f12 (sub \$f01 (func))))
  (rec (type \$f21 (sub (func))) (type \$f22 (sub \$f11 (func))))
  (func (export "f") (type \$f21))
)`);

// ./test/core/gc/type-subtyping.wast:712
register($43, `M11`);

// ./test/core/gc/type-subtyping.wast:713
assert_unlinkable(
  () => instantiate(`(module
    (rec (type \$f01 (sub (func))) (type \$f02 (sub \$f01 (func))))
    (rec (type \$f11 (sub (func))) (type \$f12 (sub \$f01 (func))))
    (func (import "M11" "f") (type \$f11))
  )`),
  `incompatible import`,
);

// ./test/core/gc/type-subtyping.wast:726
assert_invalid(
  () => instantiate(`(module
    (type \$t (func))
    (type \$s (sub \$t (func)))
  )`),
  `sub type`,
);

// ./test/core/gc/type-subtyping.wast:734
assert_invalid(
  () => instantiate(`(module
    (type \$t (struct))
    (type \$s (sub \$t (struct)))
  )`),
  `sub type`,
);

// ./test/core/gc/type-subtyping.wast:742
assert_invalid(
  () => instantiate(`(module
    (type \$t (sub final (func)))
    (type \$s (sub \$t (func)))
  )`),
  `sub type`,
);

// ./test/core/gc/type-subtyping.wast:750
assert_invalid(
  () => instantiate(`(module
    (type \$t (sub (func)))
    (type \$s (sub final \$t (func)))
    (type \$u (sub \$s (func)))
  )`),
  `sub type`,
);

// ./test/core/gc/type-subtyping.wast:763
assert_invalid(
  () => instantiate(`(module
    (type \$a0 (sub (array i32)))
    (type \$s0 (sub \$a0 (struct)))
  )`),
  `sub type`,
);

// ./test/core/gc/type-subtyping.wast:771
assert_invalid(
  () => instantiate(`(module
    (type \$f0 (sub (func (param i32) (result i32))))
    (type \$s0 (sub \$f0 (struct)))
  )`),
  `sub type`,
);

// ./test/core/gc/type-subtyping.wast:779
assert_invalid(
  () => instantiate(`(module
    (type \$s0 (sub (struct)))
    (type \$a0 (sub \$s0 (array i32)))
  )`),
  `sub type`,
);

// ./test/core/gc/type-subtyping.wast:787
assert_invalid(
  () => instantiate(`(module
    (type \$f0 (sub (func (param i32) (result i32))))
    (type \$a0 (sub \$f0 (array i32)))
  )`),
  `sub type`,
);

// ./test/core/gc/type-subtyping.wast:795
assert_invalid(
  () => instantiate(`(module
    (type \$s0 (sub (struct)))
    (type \$f0 (sub \$s0 (func (param i32) (result i32))))
  )`),
  `sub type`,
);

// ./test/core/gc/type-subtyping.wast:803
assert_invalid(
  () => instantiate(`(module
    (type \$a0 (sub (array i32)))
    (type \$f0 (sub \$a0 (func (param i32) (result i32))))
  )`),
  `sub type`,
);

// ./test/core/gc/type-subtyping.wast:811
assert_invalid(
  () => instantiate(`(module
    (type \$a0 (sub (array i32)))
    (type \$a1 (sub \$a0 (array i64)))
  )`),
  `sub type`,
);

// ./test/core/gc/type-subtyping.wast:819
assert_invalid(
  () => instantiate(`(module
    (type \$s0 (sub (struct (field i32))))
    (type \$s1 (sub \$s0 (struct (field i64))))
  )`),
  `sub type`,
);

// ./test/core/gc/type-subtyping.wast:827
assert_invalid(
  () => instantiate(`(module
    (type \$a (sub (array (ref none))))
    (type \$b (sub \$a (array (ref any))))
  )`),
  `sub type 1 does not match super type`,
);

// ./test/core/gc/type-subtyping.wast:835
assert_invalid(
  () => instantiate(`(module
    (type \$a (sub (array (mut (ref any)))))
    (type \$b (sub \$a (array (mut (ref none)))))
  )`),
  `sub type 1 does not match super type`,
);

// ./test/core/gc/type-subtyping.wast:843
assert_invalid(
  () => instantiate(`(module
    (type \$a (sub (array (mut (ref any)))))
    (type \$b (sub \$a (array (ref any))))
  )`),
  `sub type 1 does not match super type`,
);

// ./test/core/gc/type-subtyping.wast:851
assert_invalid(
  () => instantiate(`(module
    (type \$a (sub (array (ref any))))
    (type \$b (sub \$a (array (mut (ref any)))))
  )`),
  `sub type 1 does not match super type`,
);

// ./test/core/gc/type-subtyping.wast:859
assert_invalid(
  () => instantiate(`(module
    (type \$a (sub (struct (field (ref none)))))
    (type \$b (sub \$a (struct (field (ref any)))))
  )`),
  `sub type 1 does not match super type`,
);

// ./test/core/gc/type-subtyping.wast:867
assert_invalid(
  () => instantiate(`(module
    (type \$a (sub (struct (field (mut (ref any))))))
    (type \$b (sub \$a (struct (field (mut (ref none))))))
  )`),
  `sub type 1 does not match super type`,
);

// ./test/core/gc/type-subtyping.wast:875
assert_invalid(
  () => instantiate(`(module
    (type \$a (sub (struct (field (mut (ref any))))))
    (type \$b (sub \$a (struct (field (ref any)))))
  )`),
  `sub type 1 does not match super type`,
);

// ./test/core/gc/type-subtyping.wast:883
assert_invalid(
  () => instantiate(`(module
    (type \$a (sub (struct (field (ref any)))))
    (type \$b (sub \$a (struct (field (mut (ref any))))))
  )`),
  `sub type 1 does not match super type`,
);

// ./test/core/gc/type-subtyping.wast:891
assert_invalid(
  () => instantiate(`(module
    (type \$f0 (sub (func)))
    (type \$f1 (sub \$f0 (func (param i32))))
  )`),
  `sub type`,
);

// ./test/core/gc/type-subtyping.wast:901
let $44 = instantiate(`(module
  (type \$t1 (sub (func (result f32))))
  (type \$t2 (sub \$t1 (func (result f32))))
  (type \$t3 (sub final \$t2 (func (result f32))))
  (rec
    (type \$t4 (func (result f32)))
    (type \$t5 (sub (func (result f32))))
    (type \$t6 (sub \$t5 (func (result f32))))
  )
  (func (export "f1") (type \$t1) (f32.const 0))
  (func (export "f2") (type \$t2) (f32.const 0))
  (func (export "f3") (type \$t3) (f32.const 0))
  (func (export "f4") (type \$t4) (f32.const 0))
  (func (export "f5") (type \$t5) (f32.const 0))
  (func (export "f6") (type \$t6) (f32.const 0))
)`);

// ./test/core/gc/type-subtyping.wast:918
assert_return(() => invoke($44, `f1`, []), [value("f32", 0)]);

// ./test/core/gc/type-subtyping.wast:919
assert_return(() => invoke($44, `f2`, []), [value("f32", 0)]);

// ./test/core/gc/type-subtyping.wast:920
assert_return(() => invoke($44, `f3`, []), [value("f32", 0)]);

// ./test/core/gc/type-subtyping.wast:921
assert_return(() => invoke($44, `f4`, []), [value("f32", 0)]);

// ./test/core/gc/type-subtyping.wast:922
assert_return(() => invoke($44, `f5`, []), [value("f32", 0)]);

// ./test/core/gc/type-subtyping.wast:923
assert_return(() => invoke($44, `f6`, []), [value("f32", 0)]);
