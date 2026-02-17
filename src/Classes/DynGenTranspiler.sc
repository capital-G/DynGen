// translates sclang code to DynGen code
// and stores the necessary context
DynGenTranspiler {
	var <>environment;
	var <>statements;
	var <>varNames;

	*new {|func|
		^super.newCopyArgs().init(func);
	}

	init {|func|
		statements = [];
		varNames = Set();
		environment = DynGenEnvironment(this);
		"Calling func now".postln;
		environment.use({func.value(environment)});
	}

	addStatement {|lhs, rhs|
		"Add statement % %".format(lhs, rhs).postln;
		statements = statements.add((lhs: lhs, rhs: rhs));
	}

	registerVar {|name|
		"register var %".format(name).postln;
		varNames = varNames.add(name);
	}

	emit {
		var code = "";
		statements.do({|statement|
			code = "%% = %;\n".format(
				code,
				statement.lhs.asDynGen,
				statement.rhs.asDynGen,
			)
		});
		^code;
	}

}

DynGenEnvironment : EnvironmentRedirect {
	var <>context;

	*new {|context|
		// "New environment w/ %".format(context).postln;
		^super.new.init(context);
	}

	init {|context_|
		context = context_;
	}

	makeVar {|name|
		"makeVar got called w/ % - %".format(name, context).postln;
		^DynGenVar(name, this.context);
	}

	add {|...args|
		"Add got called w/ %".format(args).postln;
	}

	at { |key|
		var dgVar;
		"At got called with % (context: %)".format(key, context).postln;

		dgVar = envir.at(key);

		if(dgVar.isNil, {
			dgVar = this.makeVar(key, context);
			envir.put(key, dgVar);
			context.registerVar(key);
		});

		^dgVar;
	}

	put { |key, value|
		var dgVar;
		"put got called w/ % - %".format(key, value).postln;
		dgVar = DynGenVar(key, context);
		context.addStatement(dgVar, value.asDynGen);
		super.put(key, dgVar);
		^dgVar;
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

	// how to implement out - which is used in assignments and not as a function
	in {
		^DynGenFuncCall(\in, [this], context);
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
		"Perform + from % on %".format(this, other);
		^DynGenBinOp.new('+', this, other, context);
	}

	- {|other|
		^DynGenBinOp.new('-', this, other, context);
	}

	* {|other|
		"Perform * from % on %".format(this, other);
		^DynGenBinOp.new('*', this, other, context);
	}

	/ {|divisor|
		"Perform / from % on %".format(this, divisor);
		^DynGenBinOp.new('/', this, divisor, context);
	}

	? {|other|
		^DynGenBinOp.new('?', this, other, context);
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

	asDynGen {
		^this;
	}

	performBinaryOpOnSimpleNumber { arg aSelector, aNumber, adverb;
		^aNumber.asDynGen.perform(aSelector, this, adverb)
	}

}

DynGenFuncCall : DynGenExpr {
	var <>funcName;
	var <>arguments;

	*new {|funcName, arguments, context|
		^super.new(context).initFuncCall(funcName, arguments);
	}

	initFuncCall {|funcName_, arguments_|
		"New func call w/ % (%)".format(funcName, arguments);
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

	printOn { |stream|
		stream << this.asDynGen;
	}
}

DynGenLiteral : DynGenExpr {
	var <> context;
	var <> value;

	*new {|value, context|
		^super.new(context).initLiteral(value);
	}

	initLiteral {|value_|
		"Init literal %".format(value_).postln;
		value = value_;
	}

	asDynGen {
		^value.asString;
	}

	printOn { |stream|
		stream << this.asDynGen;
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

	printOn { |stream|
		stream << this.asDynGen;
	}
}

DynGenVar : DynGenExpr {
	var <name;

	*new {|name, context|
		"new dyngenvar w/ % %".format(name, context).postln;
		^super.new(context).init(name);
	}

	init {|name_|
		"New variable %".format(name_).postln;
		context.registerVar(name_);
		name = name_;
	}

	asDynGen {
		^name.asString;
	}

	printOn { |stream|
		stream << this.asDynGen;
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

	// no asDynGen or printOn since dyngen
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
