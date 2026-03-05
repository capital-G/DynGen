// translates sclang code to DynGen code
// and stores the necessary context
DynGenTranspiler {
	// this is public such that the value can be fixed
	// for testing etc.
	classvar >tempCounter = 0;
	var <environment;

	*new {|func|
		^super.newCopyArgs().init(func);
	}

	init {|func|
		environment = PrDynGenEnvironment_(this);
		environment.use({func.value(environment)});
		environment.prFlushDueStatements;
	}

	compile {
		^environment.compile;
	}

	paramCompile {
		^DynGenTranspiler.paramCompile([this]);
	}

	*paramCompile {|transpilers|
		// combine params of all transpilers
		var params = ();
		var output = "";
		transpilers.asArray.do({|transpiler|
			transpiler.environment[\p].params.pairsDo({|key, value|
				if(params[key.asSymbol].isNil, {
					params[key] = value;
				});
			})
		});
		params.pairsDo({|key, value|
			output = "%@param %: init = %, type = %\n".format(
				output,
				key,
				value.init ? 0.0,
				value.type ? "lin",
			);
		});
		^output;
	}

	*nextTempVariable {|context|
		var temp = PrDynGenVar_("temp%".format(
			tempCounter,
		).asSymbol, context);
		tempCounter = tempCounter + 1;
		^temp;
	}
}

PrDynGenEnvironment_ : EnvironmentRedirect {
	var <context;
	var <sequence;
	// due statements are emitted while parsing the statements
	// if the statement is not assigned, it will be put into
	// the sequence such that it gets executed, else it will be discarded.
	var <dueStatements;
	var tempVariableCount = 0;

	*new {|context|
		^super.new.init(context);
	}

	init {|context_|
		context = context_;
		dueStatements = [];
		sequence = PrDynGenSequence_([], context);
		envir.put(\p, PrDynGenParamAccessor_(context));
	}

	makeVar {|name|
		^PrDynGenVar_(name, this.context);
	}

	addStatement { |statement|
		sequence = sequence.add(statement);
	}

	addDueStatement {|dueStatement|
		dueStatements = dueStatements.add(dueStatement);
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
		var statement;
		var dgVar;

		this.prFlushDueStatements(value);

		dgVar = PrDynGenVar_(key, context);

		// if value/rhs is a function, we use a custom wrapper
		statement = if(value.isKindOf(Function), {
			PrDynGenUserFunc_(key, value, context);
		}, {
			 PrDynGenAssignment_(
				lhs: dgVar,
				rhs: value,
				context: context,
			);
		});

		this.addStatement(statement);

		super.put(key, dgVar);
		^dgVar;
	}

	compile {
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

// kept "public" b/c of autocomplete
DynGenExpr {
	var <context;

	*new {|context|
		^super.newCopyArgs(context);
	}

	// functions
	wrap {|min, max|
		^PrDynGenFuncCall_(\wrap, [this, min, max], context);
	}

	clip {|lo, hi|
		^PrDynGenFuncCall_(\clip, [this, lo, hi], context);
	}

	fold {|lo, hi|
		^PrDynGenFuncCall_(\fold, [this, lo, hi], context);
	}

	mod {|hi|
		^PrDynGenFuncCall_(\mod, [this, hi], context);
	}

	lin {|a, b|
		^PrDynGenFuncCall_(\lin, [this, a, b], context);
	}

	cubic {|a, b, c, d|
		^PrDynGenFuncCall_(\cubic, [this, a, b, c, d], context);
	}

	bufRead {|frame, chan=0|
		^PrDynGenFuncCall_(\bufRead, [this, frame, chan], context);
	}

	bufReadL{|frame, chan=0|
		^PrDynGenFuncCall_(\bufReadL, [this, frame, chan], context);
	}

	bufReadC {|frame, chan=0|
		^PrDynGenFuncCall_(\bufReadC, [this, frame, chan], context);
	}

	bufWrite {|bufNum, frame, chan=0|
		^PrDynGenFuncCall_(\bufWrite, [bufNum, frame, this, chan], context);
	}

	bufRate {
		^PrDynGenFuncCall_(\bufRate, [this], context);
	}

	bufChannels {
		^PrDynGenFuncCall_(\bufChannels, [this], context);
	}

	bufFrames {
		^PrDynGenFuncCall_(\bufFrames, [this], context);
	}

	in {
		^PrDynGenFuncCall_(\in, [this], context);
	}

	// maybe this is too hacky?
	out {|chan|
		^PrDynGenBinOp_("=", "out(%)".format(chan), this, context);
	}

	delta {|state|
		state = state ? DynGenTranspiler.nextTempVariable(context);
		^PrDynGenFuncCall_(\delta, [state, this], context);
	}

	history {|state|
		state = state ? DynGenTranspiler.nextTempVariable(context);
		^PrDynGenFuncCall_(\history, [state, this], context);
	}

	latch {|trigger, state|
		state = state ? DynGenTranspiler.nextTempVariable(context);
		^PrDynGenFuncCall_(\latch, [state, this, trigger], context);
	}

	print {
		^PrDynGenFuncCall_(\print, [this], context);
	}

	printMem {|size|
		^PrDynGenFuncCall_(\printMem, [this, size], context);
	}

	poll {|rate=10.0|
		^PrDynGenFuncCall_(\poll, [this, rate], context);
	}

	// eel2 built-in unary functions
	sign {
		^PrDynGenFuncCall_(\sign, [this], context);
	}

	rand {
		^PrDynGenFuncCall_(\rand, [this], context);
	}

	floor {
		^PrDynGenFuncCall_(\floor, [this], context);
	}

	ceil {
		^PrDynGenFuncCall_(\ceil, [this], context);
	}

	invsqrt {
		^PrDynGenFuncCall_(\invsqrt, [this], context);
	}

	sin {
		^PrDynGenFuncCall_(\sin, [this], context);
	}

	cos {
		^PrDynGenFuncCall_(\cos, [this], context);
	}

	tan {
		^PrDynGenFuncCall_(\tan, [this], context);
	}

	asin {
		^PrDynGenFuncCall_(\asin, [this], context);
	}

	acos {
		^PrDynGenFuncCall_(\acos, [this], context);
	}

	atan {
		^PrDynGenFuncCall_(\atan, [this], context);
	}

	sqr {
		^PrDynGenFuncCall_(\sqr, [this], context);
	}

	sqrt {
		^PrDynGenFuncCall_(\sqrt, [this], context);
	}

	exp {
		^PrDynGenFuncCall_(\exp, [this], context);
	}

	log {
		^PrDynGenFuncCall_(\log, [this], context);
	}

	log10 {
		^PrDynGenFuncCall_(\log10, [this], context);
	}

	// unary ops
	not {
		^PrDynGenUnaryOp_("!", this, context);
	}

	neg {
		^PrDynGenUnaryOp_("-", this, context);
	}

	// binops
	// can't implement ^ - use pow instead

	% { |denominator|
		^PrDynGenBinOp_('%', this, denominator, context);
	}

	<< {|shiftAmount|
		^PrDynGenBinOp_('<<', this, shiftAmount, context);
	}

	>> {|shiftAmount|
		^PrDynGenBinOp_('>>', this, shiftAmount, context);
	}

	+ {|other|
		^PrDynGenBinOp_('+', this, other, context);
	}

	- {|other|
		^PrDynGenBinOp_('-', this, other, context);
	}

	* {|other|
		^PrDynGenBinOp_('*', this, other, context);
	}

	/ {|divisor|
		^PrDynGenBinOp_('/', this, divisor, context);
	}

	| {|other|
		^PrDynGenBinOp_('|', this, other, context);
	}

	& {|other|
		^PrDynGenBinOp_('&', this, other, context);
	}

	// can't use ~ - this is xor
	xor {|other|
		^PrDynGenBinOp_('~', this, other, context);
	}

	== {|other|
		^PrDynGenBinOp_('==', this, other, context);
	}

	=== {|other|
		^PrDynGenBinOp_('===', this, other, context);
	}

	!= {|other|
		^PrDynGenBinOp_('!=', this, other, context);
	}

	!== {|other|
		^PrDynGenBinOp_('!==', this, other, context);
	}

	< {|other|
		^PrDynGenBinOp_('<', this, other, context);
	}

	> {|other|
		^PrDynGenBinOp_('>', this, other, context);
	}

	<= {|other|
		^PrDynGenBinOp_('<=', this, other, context);
	}

	>= {|other|
		^PrDynGenBinOp_('>=', this, other, context);
	}

	|| {|other|
		^PrDynGenBinOp_('||', this, other, context);
	}

	&& {|other|
		^PrDynGenBinOp_('&&', this, other, context);
	}

	*= {|other|
		^PrDynGenBinOp_('*=', this, other, context);
	}

	/= {|divisor|
		^PrDynGenBinOp_('/=', this, divisor, context);
	}

	%= {|divisor|
		^PrDynGenBinOp_('%=', this, divisor, context);
	}

	// do not implement ^=

	+= {|other|
		^PrDynGenBinOp_('+=', this, other, context);
	}

	-= {|other|
		^PrDynGenBinOp_('-=', this, other, context);
	}

	|= {|other|
		^PrDynGenBinOp_('|=', this, other, context);
	}

	&= {|other|
		^PrDynGenBinOp_('&=', this, other, context);
	}

	abs {
		^PrDynGenFuncCall_(\abs, [this], context);
	}

	min {|other|
		^PrDynGenFuncCall_(\min, [this, other], context);
	}

	max {|other|
		^PrDynGenFuncCall_(\max, [this, other], context);
	}

	atan2 {|other|
		^PrDynGenFuncCall_(\atan2, [this, other], context);
	}

	pow {|exponent|
		^PrDynGenFuncCall_(\pow, [this, exponent], context);
	}

	at {|offset|
		^PrDynGenMemoryAccess_(this, offset, context);
	}

	// put is an assignment - e.g. x[20] = sin(0.5);
	put {|offset, value|
		var lhs = PrDynGenMemoryAccess_(this, offset, context);
		var assignment = PrDynGenAssignment_(
			lhs: lhs,
			rhs: value,
			context: context,
		);
		context.environment.sequence.add(assignment);
		^assignment
	}

	ifTrue {|trueFunc, falseFunc|
		var func = if(falseFunc.isNil, {
			PrDynGenBinOp_(
				op: '?',
				left: this,
				right: trueFunc,
				context: context,
			);
		}, {
			PrDynGenTernaryOp_(
				opA: '?',
				opB: ':',
				selector: this,
				branchA: trueFunc,
				branchB: falseFunc,
				context: context,
			);
		});

		// if can also be used w/o assignment
		context.environment.addDueStatement(func);
		^func;
	}

	loop {|code|
		var func = PrDynGenFuncCall_(
			funcName: 'loop',
			arguments: [this, code],
			context: context,
		);
		// loop can also be used w/o assignment
		context.environment.addDueStatement(func);
		^func;
	}

	// only allow while with condition
	// this is renamed to avoid inline optimization
	whileTrue {|code|
		var func = PrDynGenDoWhile_(
			condition: this,
			code: code,
			context: context,
		);
		// while can also be used w/o assignment
		context.environment.addDueStatement(func);
		^func;
	}

	// fft
	mdct {|size|
		^PrDynGenFuncCall_(
			funcName: 'mdct',
			arguments: [this, size],
			context: context,
		);
	}

	imdct {|size|
		^PrDynGenFuncCall_(
			arguments: 'imdct',
			context: [this, size],
			funcName: context,
		);
	}

	fft {|size|
		^PrDynGenFuncCall_(
			funcName: 'fft',
			arguments: [this, size],
			context: context,
		);
	}

	ifft {|size|
		^PrDynGenFuncCall_(
			funcName: 'ifft',
			arguments: [this, size],
			context: context,
		);
	}

	fftReal {|size|
		^PrDynGenFuncCall_(
			funcName: 'fft_real',
			arguments: [this, size],
			context: context,
		);
	}

	ifftReal {|size|
		^PrDynGenFuncCall_(
			funcName: 'ifft_real',
			arguments: [this, size],
			context: context,
		);
	}

	fftPermute {|size|
		^PrDynGenFuncCall_(
			funcName: 'fft_permute',
			arguments: [this, size],
			context: context,
		);
	}

	fftiPermute {|size|
		^PrDynGenFuncCall_(
			funcName: 'fft_ipermute',
			arguments: [this, size],
			context: context,
		);
	}

	convolveC {|size, dest|
		^PrDynGenFuncCall_(
			funcName: 'convolve_c',
			arguments: [dest, this, size],
			context: context,
		);
	}

	// memory
	freemBuf {
		^PrDynGenFuncCall_(
			funcName: 'freembuf',
			arguments: [this],
			context: context,
		);
	}

	memCopy {|length, dest|
		^PrDynGenFuncCall_(
			funcName: 'memcopy',
			arguments: [dest, this, length],
			context: context,
		);
	}

	memSet {|length, dest|
		^PrDynGenFuncCall_(
			funcName: 'memset',
			arguments: [dest, this, length],
			context: context,
		)
	}

	memMultiplySum {|length, other|
		^PrDynGenFuncCall_(
			funcName: 'mem_multiply_sum',
			arguments: [this, other, length],
			context: context,
		);
	}

	memInsertShuffle {|length, value|
		^PrDynGenFuncCall_(
			funcName: 'mem_insert_shuffle',
			arguments: [this, length, value],
			context: context,
		);
	}

	memTop {
		^PrDynGenFuncCall_(
			funcName: '__memtop',
			arguments: [],
			context: context,
		);
	}

	// stack
	stackPush {
		^PrDynGenFuncCall_(
			funcName: 'stack_push',
			arguments: [this],
			context: context,
		);
	}

	stackPop {
		^PrDynGenFuncCall_(
			funcName: 'stack_pop',
			arguments: [this],
			context: context,
		);
	}

	stackPeek {
		^PrDynGenFuncCall_(
			funcName: 'stack_peek',
			arguments: [this],
			context: context,
		);
	}

	stackExchange {
		^PrDynGenFuncCall_(
			funcName: 'stack_exch',
			arguments: [this],
			context: context,
		);
	}

	asDynGen {
		^this;
	}

	performBinaryOpOnSimpleNumber { arg aSelector, aNumber, adverb;
		^aNumber.asDynGen.perform(aSelector, this, adverb)
	}
}

PrDynGenParamAccessor_ {
	var <context;
	var <params;

	*new {|context|
		^super.newCopyArgs(context).initParamAccessor;
	}

	initParamAccessor {
		params = ();
	}

	at {|key|
		// maybe we should cache this?
		^createParam(key);
	}

	createParam {|name, init=0.0, type=\lin, spec=\unipolar|
		var res = params[name.asSymbol];
		if(res.isNil, {
			res = PrDynGenParam_(
				name,
				init,
				type,
				spec,
				context,
			);
			params[name.asSymbol] = res;
		});
		^res;
	}

	doesNotUnderstand {|selector ...args, kwargs|
		^this.performArgs(\createParam, [selector]++args, kwargs);
	}
}

PrDynGenParam_ : DynGenExpr {
	var <name;
	var <init;
	var <type;
	var <spec;

	*new {|name, init, type, spec, context|
		^super.new(context).initParam(name, init, type, spec);
	}

	initParam {|name_, init_, type_, spec_|
		name = name_;
		init = init_;
		type = type_;
		spec = spec_;
	}

	asDynGen {
		^"_%".format(name.asString);
	}
}

PrDynGenAssignment_ : DynGenExpr {
	var <lhs;
	var <rhs;

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

PrDynGenSequence_ : DynGenExpr {
	var <expressions;

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

PrDynGenFuncCall_ : DynGenExpr {
	var <funcName;
	var <arguments;

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

PrDynGenLiteral_ : DynGenExpr {
	var <context;
	var <value;

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

PrDynGenMemoryAccess_ : DynGenExpr {
	var <variable;
	var <offset;

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

PrDynGenUnaryOp_ : DynGenExpr {
	var <op;
	var <value;

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

PrDynGenBinOp_ : DynGenExpr {
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

PrDynGenTernaryOp_ : DynGenExpr {
	var <opA;
	var <opB;
	var <selector;
	var <branchA;
	var <branchB;

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

PrDynGenDoWhile_ : DynGenExpr {
	var <condition;
	var <code;

	*new {|condition, code, context|
		^super.new(context).initDoWhile(condition, code);
	}

	initDoWhile {|condition_, code_|
		condition = condition_;
		code = code_;
	}

	asDynGen {
		^"while (%) (%)".format(
			condition.asDynGen,
			code.asDynGen,
		);
	}
}

PrDynGenVar_ : DynGenExpr {
	var <name;

	*new {|name, context|
		^super.new(context).initVar(name);
	}

	initVar {|name_|
		name = name_;
	}

	value {|...args|
		var call = PrDynGenFuncCall_(
			funcName: name,
			arguments: args,
			context: context,
		);
		context.environment.addDueStatement(call);
		^call;
	}

	asDynGen {
		^name.asString;
	}
}

PrDynGenUserFunc_ : DynGenExpr {
	var <name;
	var <func;

	*new {|name, func, context|
		^super.new(context).initUserFunc(name, func);
	}

	initUserFunc {|name_, func_|
		name = name_;
		func = func_;
	}

	asDynGen {
		var locals = this.prLocalNames;
		if(locals.size>0, {
			^"function %(%) local(%) %".format(
				name,
				this.prArgNames.join(" "),
				locals.join(" "),
				func.asDynGen,
			);
		}, {
			^"function %(%) %".format(
				name,
				func.argNames.join(" "),
				func.asDynGen
			);
		});
	}

	prArgNames {
		^func.argNames.select({|name|
			name.asString.beginsWith("l_").not;
		});
	}

	prLocalNames {
		^func.argNames.select({|name|
			name.asString.beginsWith("l_");
		}).collect({|name|
			name.asString.replace("l_", "").asSymbol;
		});
	}
}

PrDynGenCollection_ : DynGenExpr {
	var <elements;

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
		^PrDynGenLiteral_(this, nil);
	}
}

+ Float {
	asDynGen {
		^PrDynGenLiteral_(this, nil);
	}
}

+ Collection {
	asDynGen {
		// @todo do some checks on the content of the collection!
		^PrDynGenCollection_(this, this.first.context);
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
