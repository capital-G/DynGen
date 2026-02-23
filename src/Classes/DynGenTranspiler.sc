// translates sclang code to DynGen code
// and stores the necessary context
DynGenTranspiler {
	var <>environment;

	*new {|func|
		^super.newCopyArgs().init(func);
	}

	init {|func|
		environment = DynGenEnvironment(this);
		environment.use({func.value(environment)});
	}

	compile {
		^environment.compile;
	}
}

DynGenEnvironment : EnvironmentRedirect {
	var <>context;
	var <>sequence;
	// due statements are emitted while parsing the statements
	// if the statement is not assigned, it will be put into
	// the sequence such that it gets executed, else it will be discarded.
	var <>dueStatements;

	*new {|context|
		^super.new.init(context);
	}

	init {|context_|
		context = context_;
		dueStatements = [];
		sequence = DynGenSequence([], context);
		envir.put(\p, DynGenParamAccessor(context));
	}

	makeVar {|name|
		^DynGenVar(name, this.context);
	}

	addStatement { |statement|
		sequence = sequence.add(statement);
	}

	at { |key|
		var dgVar = envir.at(key);

		if(dgVar.isNil, {
			dgVar = this.makeVar(key, context);
			envir.put(key, dgVar);
		});

		^dgVar;
	}

	put {|key, value|
		var assignment;
		var dgVar;

		this.prFlushDueStatements(value);

		dgVar = DynGenVar(key, context);

		assignment = DynGenAssignment(
			lhs: dgVar,
			rhs: value.asDynGen,
			context: context,
		);

		this.addStatement(assignment);

		super.put(key, dgVar);
		^dgVar;
	}

	compile {
		this.prFlushDueStatements;
		^sequence.asDynGen;
	}

	prFlushDueStatements {|currentStatement|
		dueStatements.do({|dueStatement|
			if(dueStatement.hash !== currentStatement.hash, {
				this.addStatement(dueStatement);
			});
		});
		dueStatements = [];
	}
}


DynGenExpr {
	var <>context;

	*new {|context|
		^super.newCopyArgs(context);
	}

	// functions
	wrap {|min, max|
		^DynGenFuncCall(\wrap, [this, min, max], context);
	}

	clip {|lo, hi|
		^DynGenFuncCall(\clip, [this, lo, hi], context);
	}

	fold {|lo, hi|
		^DynGenFuncCall(\fold, [this, lo, hi], context);
	}

	mod {|hi|
		^DynGenFuncCall(\mod, [this, hi], context);
	}

	lin {|a, b|
		^DynGenFuncCall(\lin, [this, a, b], context);
	}

	cubic {|a, b, c, d|
		^DynGenFuncCall(\cubic, [this, a, b, c, d], context);
	}

	bufRead {|frame, chan=0|
		^DynGenFuncCall(\bufRead, [this, frame, chan], context);
	}

	bufReadL{|frame, chan=0|
		^DynGenFuncCall(\bufReadL, [this, frame, chan], context);
	}

	bufReadC {|frame, chan=0|
		^DynGenFuncCall(\bufReadC, [this, frame, chan], context);
	}

	bufWrite {|bufNum, frame, chan=0|
		^DynGenFuncCall(\bufWrite, [bufNum, frame, this, chan], context);
	}

	bufRate {
		^DynGenFuncCall(\bufRate, [this], context);
	}

	bufChannels {
		^DynGenFuncCall(\bufChannels, [this], context);
	}

	bufFrames {
		^DynGenFuncCall(\bufFrames, [this], context);
	}

	in {
		^DynGenFuncCall(\in, [this], context);
	}

	// maybe this is too hacky?
	out {|chan|
		^DynGenBinOp("=", "out(%)".format(chan), this, context);
	}

	delta {|state|
		^DynGenFuncCall(\delta, [state, this], context);
	}

	history {|state|
		^DynGenFuncCall(\history, [state, this], context);
	}

	latch {|trigger, state|
		^DynGenFuncCall(\latch, [state, this, trigger], context);
	}

	print {
		^DynGenFuncCall(\print, [this], context);
	}

	printMem {|size|
		^DynGenFuncCall(\printMem, [this, size], context);
	}

	poll {|rate=10.0|
		^DynGenFuncCall(\poll, [this, rate], context);
	}

	// eel2 built-in unary functions
	sign {
		^DynGenFuncCall(\sign, [this], context);
	}

	rand {
		^DynGenFuncCall(\rand, [this], context);
	}

	floor {
		^DynGenFuncCall(\floor, [this], context);
	}

	ceil {
		^DynGenFuncCall(\ceil, [this], context);
	}

	invsqrt {
		^DynGenFuncCall(\invsqrt, [this], context);
	}

	sin {
		"Perform sin on %".format(this);
		^DynGenFuncCall(\sin, [this], context);
	}

	cos {
		^DynGenFuncCall(\cos, [this], context);
	}

	tan {
		^DynGenFuncCall(\tan, [this], context);
	}

	asin {
		^DynGenFuncCall(\asin, [this], context);
	}

	acos {
		^DynGenFuncCall(\acos, [this], context);
	}

	atan {
		^DynGenFuncCall(\atan, [this], context);
	}

	sqr {
		^DynGenFuncCall(\sqr, [this], context);
	}

	sqrt {
		^DynGenFuncCall(\sqrt, [this], context);
	}

	exp {
		^DynGenFuncCall(\exp, [this], context);
	}

	log {
		^DynGenFuncCall(\log, [this], context);
	}

	log10 {
		^DynGenFuncCall(\log10, [this], context);
	}

	// unary ops
	not {
		^DynGenUnaryOp("!", this, context);
	}

	neg {
		^DynGenUnaryOp("-", this, context);
	}

	// binops
	// can't implement ^ - use pow instead

	% { |denominator|
		^DynGenBinOp('%', this, denominator, context);
	}

	<< {|shiftAmount|
		^DynGenBinOp('<<', this, shiftAmount, context);
	}

	>> {|shiftAmount|
		^DynGenBinOp('>>', this, shiftAmount, context);
	}

	+ {|other|
		^DynGenBinOp('+', this, other, context);
	}

	- {|other|
		^DynGenBinOp('-', this, other, context);
	}

	* {|other|
		^DynGenBinOp('*', this, other, context);
	}

	/ {|divisor|
		^DynGenBinOp('/', this, divisor, context);
	}

	| {|other|
		^DynGenBinOp('|', this, other, context);
	}

	& {|other|
		^DynGenBinOp('&', this, other, context);
	}

	// can't use ~ - this is xor
	xor {|other|
		^DynGenBinOp('~', this, other, context);
	}

	== {|other|
		^DynGenBinOp('==', this, other, context);
	}

	=== {|other|
		^DynGenBinOp('===', this, other, context);
	}

	!= {|other|
		^DynGenBinOp('!=', this, other, context);
	}

	!== {|other|
		^DynGenBinOp('!==', this, other, context);
	}

	< {|other|
		^DynGenBinOp('<', this, other, context);
	}

	> {|other|
		^DynGenBinOp('>', this, other, context);
	}

	<= {|other|
		^DynGenBinOp('<=', this, other, context);
	}

	>= {|other|
		^DynGenBinOp('>=', this, other, context);
	}

	|| {|other|
		^DynGenBinOp('||', this, other, context);
	}

	&& {|other|
		^DynGenBinOp('&&', this, other, context);
	}

	*= {|other|
		^DynGenBinOp('*=', this, other, context);
	}

	/= {|divisor|
		^DynGenBinOp('/=', this, divisor, context);
	}

	%= {|divisor|
		^DynGenBinOp('%=', this, divisor, context);
	}

	// do not implement ^=

	+= {|other|
		^DynGenBinOp('+=', this, other, context);
	}

	-= {|other|
		^DynGenBinOp('-=', this, other, context);
	}

	|= {|other|
		^DynGenBinOp('|=', this, other, context);
	}

	&= {|other|
		^DynGenBinOp('&=', this, other, context);
	}

	min {|other|
		^DynGenFuncCall(\min, [this, other], context);
	}

	max {|other|
		^DynGenFuncCall(\max, [this, other], context);
	}

	atan2 {|other|
		^DynGenFuncCall(\atan2, [this, other], context);
	}

	pow {|exponent|
		^DynGenFuncCall(\pow, [this, exponent], context);
	}

	at {|offset|
		^DynGenMemoryAccess(this, offset, context);
	}

	// put is an assignment - e.g. x[20] = sin(0.5);
	put {|offset, value|
		var lhs = DynGenMemoryAccess(this, offset, context);
		var assignment = DynGenAssignment(
			lhs: lhs,
			rhs: value,
			context: context,
		);
		context.environment.sequence.add(assignment);
		^assignment
	}

	ifTrue {|trueFunc, falseFunc|
		var func = if(falseFunc.isNil, {
			DynGenBinOp(
				op: '?',
				left: this,
				right: trueFunc,
				context: context,
			);
		}, {
			DynGenTernaryOp(
				opA: '?',
				opB: ':',
				selector: this,
				branchA: trueFunc,
				branchB: falseFunc,
				context: context,
			);
		});

		// if can also be used w/o assignment
		context.environment.dueStatements = context.environment.dueStatements.add(func);
		^func;

	}

	asDynGen {
		^this;
	}

	performBinaryOpOnSimpleNumber { arg aSelector, aNumber, adverb;
		^aNumber.asDynGen.perform(aSelector, this, adverb)
	}
}

DynGenParamAccessor {
	var <>context;

	*new {|context|
		^super.newCopyArgs(context);
	}

	at {|key|
		// maybe we should cache this?
		^createParam(key);
	}

	createParam {|name, default=0.0, spec=\unipolar|
		^DynGenParam(
			name,
			default,
			spec,
			context,
		);
	}

	doesNotUnderstand {|selector ...args, kwargs|
		^this.performArgs(\createParam, [selector]++args, kwargs);
	}
}

DynGenParam : DynGenExpr {
	var <>name;
	var <>default;
	var <>spec;

	*new {|name, default, spec, context|
		^super.new(context).initParam(name, default, spec);
	}

	initParam {|name_, default_, spec_|
		name = name_;
		default = default_;
		spec = spec_;
	}

	asDynGen {
		^"_%".format(name.asString);
	}
}

DynGenAssignment : DynGenExpr {
	var <>lhs;
	var <>rhs;

	*new {|lhs, rhs, context|
		^super.new(context).initAssignment(lhs, rhs);
	}

	initAssignment {|lhs_, rhs_|
		lhs = lhs_;
		rhs = rhs_;
	}

	asDynGen {
		^"% = %".format(lhs.asDynGen, rhs.asDynGen);
	}
}

DynGenSequence : DynGenExpr {
	var <>expressions;

	*new {|expressions, context|
		^super.new(context).initSequence(expressions);
	}

	initSequence {|expressions_|
		expressions = expressions_.asArray;
	}

	add {|expression|
		expressions = expressions.add(expression);
	}

	asDynGen {
		^expressions.collect({|expression|
			"%;".format(expression.asDynGen);
		}).join("\n");
	}
}

DynGenFuncCall : DynGenExpr {
	var <>funcName;
	var <>arguments;

	*new {|funcName, arguments, context|
		^super.new(context).initFuncCall(funcName, arguments);
	}

	initFuncCall {|funcName_, arguments_|
		funcName = funcName_;
		arguments = arguments_;
	}

	asDynGen {
		^"%(%)".format(
			funcName,
			arguments.collect({|argument|
				argument.asDynGen;
			}).join(", ");
		)
	}
}

DynGenLiteral : DynGenExpr {
	var <> context;
	var <> value;

	*new {|value, context|
		^super.new(context).initLiteral(value);
	}

	initLiteral {|value_|
		value = value_;
	}

	asDynGen {
		^value.asString;
	}

	printOn {|stream|
		stream << this.asDynGen;
	}
}

DynGenMemoryAccess : DynGenExpr {
	var <>variable;
	var <>offset;

	*new {|variable, offset, context|
		^super.new(context).initMemoryAccess(variable, offset);
	}

	initMemoryAccess {|variable_, offset_|
		variable = variable_;
		offset = offset_;
	}

	asDynGen {
		^"%[%]".format(variable.asDynGen, offset.asDynGen);
	}
}

DynGenUnaryOp : DynGenExpr {
	var <>op;
	var <>value;

	*new {|op, value, context|
		^super.new(context).initUnaryOp(op, value);
	}

	initUnaryOp {|op_, value_|
		op = op_;
		value = value_;
	}

	asDynGen {
		^"%%".format(op, value.asDynGen);
	}
}

DynGenBinOp : DynGenExpr {
	var <>op;
	var <>left;
	var <>right;

	*new {|op, left, right, context|
		^super.new(context).initBinOp(op, left, right);
	}

	initBinOp {|o, l, r|
		op = o;
		left = l;
		right = r;
	}

	asDynGen {
		^"(% % %)".format(
			left.asDynGen,
			op,
			right.asDynGen,
		)
	}
}

DynGenTernaryOp : DynGenExpr {
	var <>opA;
	var <>opB;
	var <>selector;
	var <>branchA;
	var <>branchB;

	*new {|opA, opB, selector, branchA, branchB, context|
		^super.new(context).initTernaryOp(
			opA,
			opB,
			selector,
			branchA,
			branchB,
		);
	}

	initTernaryOp {|opA_, opB_, selector_, branchA_, branchB_|
		opA = opA_;
		opB = opB_;
		selector = selector_;
		branchA = branchA_;
		branchB = branchB_;
	}

	asDynGen {
		^"% % % % %".format(
			selector.asDynGen,
			opA,
			branchA.asDynGen,
			opB,
			branchB.asDynGen,
		);
	}
}

DynGenVar : DynGenExpr {
	var <name;

	*new {|name, context|
		^super.new(context).init(name);
	}

	init {|name_|
		name = name_;
	}

	asDynGen {
		^name.asString;
	}
}

DynGenCollection : DynGenExpr {
	var <>elements;

	*new {|elements, context|
		^super.new(context).initCollection(elements);
	}

	initCollection {|elements_|
		elements = elements_;
	}

	sum {
		^elements.reduce({|a, b| a+b;});
	}

	mean {
		^(this.sum/elements.size);
	}

	at {|index|
		^elements[index];
	}

	// no asDynGen since dyngen
	// has no collections - these only act as
	// containers for sc variables to operate
	// meta operations on them
}

+ Integer {
	asDynGen {
		^DynGenLiteral(this, nil);
	}
}

+ Float {
	asDynGen {
		^DynGenLiteral(this, nil);
	}
}

+ Collection {
	asDynGen {
		// @todo do some checks on the content of the collection!
		^DynGenCollection(this, this.first.context);
	}
}

+ String {
	asDynGen {
		^this;
	}
}

+ Function {
	asDynGen {
		^"(%)".format(DynGenTranspiler(this).compile).replace("\n", " ");
	}
}