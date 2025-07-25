use alloc::vec::Vec;
use core::hash::Hash;

use crate::diagnostic_filter::DiagnosticFilterNode;
use crate::front::wgsl::parse::directive::enable_extension::EnableExtensions;
use crate::front::wgsl::parse::number::Number;
use crate::front::wgsl::Scalar;
use crate::{Arena, FastIndexSet, Handle, Span};

#[derive(Debug, Default)]
pub struct TranslationUnit<'a> {
    pub enable_extensions: EnableExtensions,
    pub decls: Arena<GlobalDecl<'a>>,
    /// The common expressions arena for the entire translation unit.
    ///
    /// All functions, global initializers, array lengths, etc. store their
    /// expressions here. We apportion these out to individual Naga
    /// [`Function`]s' expression arenas at lowering time. Keeping them all in a
    /// single arena simplifies handling of things like array lengths (which are
    /// effectively global and thus don't clearly belong to any function) and
    /// initializers (which can appear in both function-local and module-scope
    /// contexts).
    ///
    /// [`Function`]: crate::Function
    pub expressions: Arena<Expression<'a>>,

    /// Non-user-defined types, like `vec4<f32>` or `array<i32, 10>`.
    ///
    /// These are referred to by `Handle<ast::Type<'a>>` values.
    /// User-defined types are referred to by name until lowering.
    pub types: Arena<Type<'a>>,

    /// Arena for all diagnostic filter rules parsed in this module, including those in functions.
    ///
    /// See [`DiagnosticFilterNode`] for details on how the tree is represented and used in
    /// validation.
    pub diagnostic_filters: Arena<DiagnosticFilterNode>,
    /// The leaf of all `diagnostic(…)` directives in this module.
    ///
    /// See [`DiagnosticFilterNode`] for details on how the tree is represented and used in
    /// validation.
    pub diagnostic_filter_leaf: Option<Handle<DiagnosticFilterNode>>,

    /// Doc comments appearing first in the file.
    /// This serves as documentation for the whole TranslationUnit.
    pub doc_comments: Vec<&'a str>,
}

#[derive(Debug, Clone, Copy)]
pub struct Ident<'a> {
    pub name: &'a str,
    pub span: Span,
}

#[derive(Debug)]
pub enum IdentExpr<'a> {
    Unresolved(&'a str),
    Local(Handle<Local>),
}

/// A reference to a module-scope definition or predeclared object.
///
/// Each [`GlobalDecl`] holds a set of these values, to be resolved to
/// specific definitions later. To support de-duplication, `Eq` and
/// `Hash` on a `Dependency` value consider only the name, not the
/// source location at which the reference occurs.
#[derive(Debug)]
pub struct Dependency<'a> {
    /// The name referred to.
    pub ident: &'a str,

    /// The location at which the reference to that name occurs.
    pub usage: Span,
}

impl Hash for Dependency<'_> {
    fn hash<H: core::hash::Hasher>(&self, state: &mut H) {
        self.ident.hash(state);
    }
}

impl PartialEq for Dependency<'_> {
    fn eq(&self, other: &Self) -> bool {
        self.ident == other.ident
    }
}

impl Eq for Dependency<'_> {}

/// A module-scope declaration.
#[derive(Debug)]
pub struct GlobalDecl<'a> {
    pub kind: GlobalDeclKind<'a>,

    /// Names of all module-scope or predeclared objects this
    /// declaration uses.
    pub dependencies: FastIndexSet<Dependency<'a>>,
}

#[derive(Debug)]
pub enum GlobalDeclKind<'a> {
    Fn(Function<'a>),
    Var(GlobalVariable<'a>),
    Const(Const<'a>),
    Override(Override<'a>),
    Struct(Struct<'a>),
    Type(TypeAlias<'a>),
    ConstAssert(Handle<Expression<'a>>),
}

#[derive(Debug)]
pub struct FunctionArgument<'a> {
    pub name: Ident<'a>,
    pub ty: Handle<Type<'a>>,
    pub binding: Option<Binding<'a>>,
    pub handle: Handle<Local>,
}

#[derive(Debug)]
pub struct FunctionResult<'a> {
    pub ty: Handle<Type<'a>>,
    pub binding: Option<Binding<'a>>,
    pub must_use: bool,
}

#[derive(Debug)]
pub struct EntryPoint<'a> {
    pub stage: crate::ShaderStage,
    pub early_depth_test: Option<crate::EarlyDepthTest>,
    pub workgroup_size: Option<[Option<Handle<Expression<'a>>>; 3]>,
}

#[cfg(doc)]
use crate::front::wgsl::lower::{LocalExpressionContext, StatementContext};

#[derive(Debug)]
pub struct Function<'a> {
    pub entry_point: Option<EntryPoint<'a>>,
    pub name: Ident<'a>,
    pub arguments: Vec<FunctionArgument<'a>>,
    pub result: Option<FunctionResult<'a>>,
    pub body: Block<'a>,
    pub diagnostic_filter_leaf: Option<Handle<DiagnosticFilterNode>>,
    pub doc_comments: Vec<&'a str>,
}

#[derive(Debug)]
pub enum Binding<'a> {
    BuiltIn(crate::BuiltIn),
    Location {
        location: Handle<Expression<'a>>,
        interpolation: Option<crate::Interpolation>,
        sampling: Option<crate::Sampling>,
        blend_src: Option<Handle<Expression<'a>>>,
    },
}

#[derive(Debug)]
pub struct ResourceBinding<'a> {
    pub group: Handle<Expression<'a>>,
    pub binding: Handle<Expression<'a>>,
}

#[derive(Debug)]
pub struct GlobalVariable<'a> {
    pub name: Ident<'a>,
    pub space: crate::AddressSpace,
    pub binding: Option<ResourceBinding<'a>>,
    pub ty: Option<Handle<Type<'a>>>,
    pub init: Option<Handle<Expression<'a>>>,
    pub doc_comments: Vec<&'a str>,
}

#[derive(Debug)]
pub struct StructMember<'a> {
    pub name: Ident<'a>,
    pub ty: Handle<Type<'a>>,
    pub binding: Option<Binding<'a>>,
    pub align: Option<Handle<Expression<'a>>>,
    pub size: Option<Handle<Expression<'a>>>,
    pub doc_comments: Vec<&'a str>,
}

#[derive(Debug)]
pub struct Struct<'a> {
    pub name: Ident<'a>,
    pub members: Vec<StructMember<'a>>,
    pub doc_comments: Vec<&'a str>,
}

#[derive(Debug)]
pub struct TypeAlias<'a> {
    pub name: Ident<'a>,
    pub ty: Handle<Type<'a>>,
}

#[derive(Debug)]
pub struct Const<'a> {
    pub name: Ident<'a>,
    pub ty: Option<Handle<Type<'a>>>,
    pub init: Handle<Expression<'a>>,
    pub doc_comments: Vec<&'a str>,
}

#[derive(Debug)]
pub struct Override<'a> {
    pub name: Ident<'a>,
    pub id: Option<Handle<Expression<'a>>>,
    pub ty: Option<Handle<Type<'a>>>,
    pub init: Option<Handle<Expression<'a>>>,
}

/// The size of an [`Array`] or [`BindingArray`].
///
/// [`Array`]: Type::Array
/// [`BindingArray`]: Type::BindingArray
#[derive(Debug, Copy, Clone)]
pub enum ArraySize<'a> {
    /// The length as a constant expression.
    Constant(Handle<Expression<'a>>),
    Dynamic,
}

#[derive(Debug)]
pub enum Type<'a> {
    Scalar(Scalar),
    Vector {
        size: crate::VectorSize,
        ty: Handle<Type<'a>>,
        ty_span: Span,
    },
    Matrix {
        columns: crate::VectorSize,
        rows: crate::VectorSize,
        ty: Handle<Type<'a>>,
        ty_span: Span,
    },
    Atomic(Scalar),
    Pointer {
        base: Handle<Type<'a>>,
        space: crate::AddressSpace,
    },
    Array {
        base: Handle<Type<'a>>,
        size: ArraySize<'a>,
    },
    Image {
        dim: crate::ImageDimension,
        arrayed: bool,
        class: crate::ImageClass,
    },
    Sampler {
        comparison: bool,
    },
    AccelerationStructure {
        vertex_return: bool,
    },
    RayQuery {
        vertex_return: bool,
    },
    RayDesc,
    RayIntersection,
    BindingArray {
        base: Handle<Type<'a>>,
        size: ArraySize<'a>,
    },

    /// A user-defined type, like a struct or a type alias.
    User(Ident<'a>),
}

#[derive(Debug, Default)]
pub struct Block<'a> {
    pub stmts: Vec<Statement<'a>>,
}

#[derive(Debug)]
pub struct Statement<'a> {
    pub kind: StatementKind<'a>,
    pub span: Span,
}

#[derive(Debug)]
pub enum StatementKind<'a> {
    LocalDecl(LocalDecl<'a>),
    Block(Block<'a>),
    If {
        condition: Handle<Expression<'a>>,
        accept: Block<'a>,
        reject: Block<'a>,
    },
    Switch {
        selector: Handle<Expression<'a>>,
        cases: Vec<SwitchCase<'a>>,
    },
    Loop {
        body: Block<'a>,
        continuing: Block<'a>,
        break_if: Option<Handle<Expression<'a>>>,
    },
    Break,
    Continue,
    Return {
        value: Option<Handle<Expression<'a>>>,
    },
    Kill,
    Call {
        function: Ident<'a>,
        arguments: Vec<Handle<Expression<'a>>>,
    },
    Assign {
        target: Handle<Expression<'a>>,
        op: Option<crate::BinaryOperator>,
        value: Handle<Expression<'a>>,
    },
    Increment(Handle<Expression<'a>>),
    Decrement(Handle<Expression<'a>>),
    Phony(Handle<Expression<'a>>),
    ConstAssert(Handle<Expression<'a>>),
}

#[derive(Debug)]
pub enum SwitchValue<'a> {
    Expr(Handle<Expression<'a>>),
    Default,
}

#[derive(Debug)]
pub struct SwitchCase<'a> {
    pub value: SwitchValue<'a>,
    pub body: Block<'a>,
    pub fall_through: bool,
}

/// A type at the head of a [`Construct`] expression.
///
/// WGSL has two types of [`type constructor expressions`]:
///
/// - Those that fully specify the type being constructed, like
///   `vec3<f32>(x,y,z)`, which obviously constructs a `vec3<f32>`.
///
/// - Those that leave the component type of the composite being constructed
///   implicit, to be inferred from the argument types, like `vec3(x,y,z)`,
///   which constructs a `vec3<T>` where `T` is the type of `x`, `y`, and `z`.
///
/// This enum represents the head type of both cases. The `PartialFoo` variants
/// represent the second case, where the component type is implicit.
///
/// This does not cover structs or types referred to by type aliases. See the
/// documentation for [`Construct`] and [`Call`] expressions for details.
///
/// [`Construct`]: Expression::Construct
/// [`type constructor expressions`]: https://gpuweb.github.io/gpuweb/wgsl/#type-constructor-expr
/// [`Call`]: Expression::Call
#[derive(Debug)]
pub enum ConstructorType<'a> {
    /// A scalar type or conversion: `f32(1)`.
    Scalar(Scalar),

    /// A vector construction whose component type is inferred from the
    /// argument: `vec3(1.0)`.
    PartialVector { size: crate::VectorSize },

    /// A vector construction whose component type is written out:
    /// `vec3<f32>(1.0)`.
    Vector {
        size: crate::VectorSize,
        ty: Handle<Type<'a>>,
        ty_span: Span,
    },

    /// A matrix construction whose component type is inferred from the
    /// argument: `mat2x2(1,2,3,4)`.
    PartialMatrix {
        columns: crate::VectorSize,
        rows: crate::VectorSize,
    },

    /// A matrix construction whose component type is written out:
    /// `mat2x2<f32>(1,2,3,4)`.
    Matrix {
        columns: crate::VectorSize,
        rows: crate::VectorSize,
        ty: Handle<Type<'a>>,
        ty_span: Span,
    },

    /// An array whose component type and size are inferred from the arguments:
    /// `array(3,4,5)`.
    PartialArray,

    /// An array whose component type and size are written out:
    /// `array<u32, 4>(3,4,5)`.
    Array {
        base: Handle<Type<'a>>,
        size: ArraySize<'a>,
    },

    /// Constructing a value of a known Naga IR type.
    ///
    /// This variant is produced only during lowering, when we have Naga types
    /// available, never during parsing.
    Type(Handle<crate::Type>),
}

#[derive(Debug, Copy, Clone)]
pub enum Literal {
    Bool(bool),
    Number(Number),
}

#[cfg(doc)]
use crate::front::wgsl::lower::Lowerer;

#[derive(Debug)]
pub enum Expression<'a> {
    Literal(Literal),
    Ident(IdentExpr<'a>),

    /// A type constructor expression.
    ///
    /// This is only used for expressions like `KEYWORD(EXPR...)` and
    /// `KEYWORD<PARAM>(EXPR...)`, where `KEYWORD` is a [type-defining keyword] like
    /// `vec3`. These keywords cannot be shadowed by user definitions, so we can
    /// tell that such an expression is a construction immediately.
    ///
    /// For ordinary identifiers, we can't tell whether an expression like
    /// `IDENTIFIER(EXPR, ...)` is a construction expression or a function call
    /// until we know `IDENTIFIER`'s definition, so we represent those as
    /// [`Call`] expressions.
    ///
    /// [type-defining keyword]: https://gpuweb.github.io/gpuweb/wgsl/#type-defining-keywords
    /// [`Call`]: Expression::Call
    Construct {
        ty: ConstructorType<'a>,
        ty_span: Span,
        components: Vec<Handle<Expression<'a>>>,
    },
    Unary {
        op: crate::UnaryOperator,
        expr: Handle<Expression<'a>>,
    },
    AddrOf(Handle<Expression<'a>>),
    Deref(Handle<Expression<'a>>),
    Binary {
        op: crate::BinaryOperator,
        left: Handle<Expression<'a>>,
        right: Handle<Expression<'a>>,
    },

    /// A function call or type constructor expression.
    ///
    /// We can't tell whether an expression like `IDENTIFIER(EXPR, ...)` is a
    /// construction expression or a function call until we know `IDENTIFIER`'s
    /// definition, so we represent everything of that form as one of these
    /// expressions until lowering. At that point, [`Lowerer::call`] has
    /// everything's definition in hand, and can decide whether to emit a Naga
    /// [`Constant`], [`As`], [`Splat`], or [`Compose`] expression.
    ///
    /// [`Lowerer::call`]: Lowerer::call
    /// [`Constant`]: crate::Expression::Constant
    /// [`As`]: crate::Expression::As
    /// [`Splat`]: crate::Expression::Splat
    /// [`Compose`]: crate::Expression::Compose
    Call {
        function: Ident<'a>,
        arguments: Vec<Handle<Expression<'a>>>,
    },
    Index {
        base: Handle<Expression<'a>>,
        index: Handle<Expression<'a>>,
    },
    Member {
        base: Handle<Expression<'a>>,
        field: Ident<'a>,
    },
    Bitcast {
        expr: Handle<Expression<'a>>,
        to: Handle<Type<'a>>,
        ty_span: Span,
    },
}

#[derive(Debug)]
pub struct LocalVariable<'a> {
    pub name: Ident<'a>,
    pub ty: Option<Handle<Type<'a>>>,
    pub init: Option<Handle<Expression<'a>>>,
    pub handle: Handle<Local>,
}

#[derive(Debug)]
pub struct Let<'a> {
    pub name: Ident<'a>,
    pub ty: Option<Handle<Type<'a>>>,
    pub init: Handle<Expression<'a>>,
    pub handle: Handle<Local>,
}

#[derive(Debug)]
pub struct LocalConst<'a> {
    pub name: Ident<'a>,
    pub ty: Option<Handle<Type<'a>>>,
    pub init: Handle<Expression<'a>>,
    pub handle: Handle<Local>,
}

#[derive(Debug)]
pub enum LocalDecl<'a> {
    Var(LocalVariable<'a>),
    Let(Let<'a>),
    Const(LocalConst<'a>),
}

#[derive(Debug)]
/// A placeholder for a local variable declaration.
///
/// See [`super::ExpressionContext::locals`] for more information.
pub struct Local;
