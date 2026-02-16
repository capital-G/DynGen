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
		environment.use({func.value()});
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
		^super.put(key, dgVar);
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

	delta {|state|
		^DynGenFuncCall(\delta, [state, this], context);
	}

	history {|state|
		^DynGenFuncCall(\history, [state, this], context);
	}


	// binops

	sin {
		"Perform sin on %".format(this);
		^DynGenFuncCall(\sin, [this], context);
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

	/ {|other|
		"Perform / from % on %".format(this, other);
		^DynGenBinOp.new('/', this, other, context);
	}

	== {|other|
		^DynGenBinOp.new('==', this, other, context);
	}

	? {|other|
		^DynGenBinOp.new('?', this, other, context);
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

// not necessary?
+ String {
	asDynGen {
		^this;
	}
}
