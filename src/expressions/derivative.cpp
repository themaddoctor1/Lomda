#include "expressions/derivative.hpp"
#include "expression.hpp"

using namespace std;

void throw_calc_err(Expression* exp) {
    throw_err("runtime", "expression '" + exp->toString() + "' is non-differentiable\n");
}

inline bool is_differentiable(Expression *exp) {
    if (dynamic_cast<const Differentiable*>(exp) != nullptr) return true;
    throw_calc_err(exp);
    return false;
}

Expression* reexpress(Value* v) {
    if (!v) return NULL;
    else if (typeid(*v) == typeid(IntVal))
        return new IntExp(((IntVal*) v)->get());
    else if (typeid(*v) == typeid(RealVal))
        return new RealExp(((RealVal*) v)->get());
    else if (typeid(*v) == typeid(BoolVal))
        return ((BoolVal*) v)->get() 
                ? (Expression*) new TrueExp
                : (Expression*) new FalseExp;
    else if (typeid(*v) == typeid(ListVal)) {
        ListVal *lv = (ListVal*) v;
        LinkedList<Expression*> *list = new LinkedList<Expression*>;

        auto it = lv->get()->iterator();
        while (it->hasNext()) list->add(list->size(), reexpress(it->next()));

        return new ListExp(list);
    } else if (typeid(*v) == typeid(LambdaVal)) {
        LambdaVal *lv = (LambdaVal*) v;

        int argc = 0;
        while (lv->getArgs()[argc] != "");

        std::string *xs = new std::string[argc+1];
        xs[argc] = "";
        while (argc--) xs[argc] = lv->getArgs()[argc];

        return new LambdaExp(xs, lv->getBody());
    } else
        return NULL;
}

Value* AndExp::derivativeOf(string x, Environment *env, Environment *denv) {
    throw_calc_err(this);
    return NULL;
}

Value* ApplyExp::derivativeOf(string x, Environment *env, Environment *denv) {
    // d/dx f(u1(x), u2(x), ...) = df/du1 * du1/dx + df/du2 * du2/dx + ...

    if (!is_differentiable(op)) return NULL;
    Value *o = op->valueOf(env);
    if (!o) return NULL;
    else if (typeid(*o) != typeid(LambdaVal)) {
        throw_type_err(op, "lambda");
        return NULL;
    }

    int argc;
    for (argc = 0; args[argc]; argc++) {
        if (!is_differentiable(args[argc]))
            return NULL;
    }

    // We will utilize the function to perform the calculations
    LambdaVal *func = (LambdaVal*) o;
    
    // Compute the derivative. init cond: d/dx f = 0
    Expression *deriv = new IntExp;
    
    for (int i = 0; args[i]; i++) {

        // For the purposes of garbage collection, we will duplicate the
        // functional arguments.
        Expression **xs = new Expression*[argc+1];
        xs[argc] = NULL;
        for (int j = 0; j < argc; j++) {
            xs[j] = args[j]->clone();
        }

        // We must apply the ith argument to the derivative
        Expression *comp = new MultExp(
            new ApplyExp(
                new DerivativeExp(op->clone(), func->getArgs()[i]),
                xs
            ),
            new DerivativeExp(args[i]->clone(), x)
        );

        if (i) deriv = new SumExp(deriv, comp);
        else {
            delete deriv;
            deriv = comp;
        }
    }
    
    Value *v = deriv->valueOf(env);

    o->rem_ref();

    delete deriv;

    return v;
}

Value* CompareExp::derivativeOf(string, Environment*, Environment*) {
    throw_calc_err(this);
    return NULL;
}

Value* DiffExp::derivativeOf(string x, Environment *env, Environment *denv) {
    if (!is_differentiable(left)) return NULL;
    if (!is_differentiable(right)) return NULL;

    Value *a = ((Differentiable*) left)->derivativeOf(x, env, denv);
    if (!a) return NULL;

    Value *b = ((Differentiable*) right)->derivativeOf(x, env, denv);
    if (!b) {
        a->rem_ref();
        return NULL;
    }

    Value *c = op(a, b);
    
    a->rem_ref(); b->rem_ref();
    return c;
}

Value* DivExp::derivativeOf(string x, Environment *env, Environment *denv) { 
    if (!is_differentiable(left)) return NULL;
    if (!is_differentiable(right)) return NULL;


    Expression *a = reexpress(((Differentiable*) left)->derivativeOf(x, env, denv));
    Expression *b = reexpress(((Differentiable*) right)->derivativeOf(x, env, denv));

    if (!a || !b) {
        delete a, b;
        return NULL;
    } else {
        // d/dx a/b = (ba' - ab') / (b^2)
        Expression *exp = new DivExp(
                            new DiffExp(
                                new MultExp(right->clone(), a),
                                new MultExp(left->clone(), b)),
                            new MultExp(right->clone(), right->clone()));
        Value *c = exp->valueOf(env);

        delete exp;
        return c;
    }

}

Value* ForExp::derivativeOf(string x, Environment *env, Environment *denv) {

    if (!is_differentiable(body)) {
        return NULL;
    } else if (!is_differentiable(set)) {
        return NULL;
    }

    // Evaluate the list
    Value *listExp = set->valueOf(env);
    if (!listExp) return NULL;
    else if (typeid(*listExp) != typeid(ListVal)) {
        throw_type_err(set, "list");
        return NULL;
    }
    // And its derivative
    Value *dlistExp = ((Differentiable*) set)->derivativeOf(x, env, denv);
    if (!dlistExp) return NULL;

    List<Value*> *list = ((ListVal*) listExp)->get();
    List<Value*> *dlist = ((ListVal*) dlistExp)->get();
    
    // Gather an iterator
    auto it = list->iterator();
    auto dit = dlist->iterator();

    // Value to be return
    Value *v = new VoidVal;

    while (it->hasNext()) {
        // Get the next item from the list
        Value *v = it->next();
        Value *dv = dit->next();
        
        // Build an environment
        Environment *e = new ExtendEnv(id, v, env);
        Environment *de = new ExtendEnv(id, dv, denv);
        
        v = ((Differentiable*) body)->derivativeOf(x, e, de);

        if (!v) {
            delete it, dit;
            return NULL;
        }

    }

    delete it, dit;

    return v;

}

Value* IfExp::derivativeOf(string x, Environment *env, Environment *denv) {
    Value *b = cond->valueOf(env);

    if (typeid(*b) != typeid(BoolVal)) {
        return NULL;
    }

    BoolVal *B = (BoolVal*) b;
    bool bRes = B->get();

    return ((Differentiable*) (bRes ? tExp : fExp))->derivativeOf(x, env, denv);
}

Value* LambdaExp::derivativeOf(string x, Environment *env, Environment *denv) {
    int argc;
    for (argc = 0; xs[argc] != ""; argc++);

    string *ids = new string[argc+1];
    ids[argc] = "";
    while (argc--)
        ids[argc] = xs[argc];

    return new LambdaVal(ids, new DerivativeExp(exp->clone(), x), env->clone());
}

Value* LetExp::derivativeOf(string x, Environment *env, Environment *denv) {
    if (!is_differentiable(body))
        return NULL;

    int argc = 0;
    for (; exps[argc]; argc++);

    // I want to make all of the lambdas recursive.
    // So, I will track my lambdas for now
    LinkedList<LambdaVal*> lambdas;
    
    // Extend the environment
    for (int i = 0; i < argc; i++) {
        if (!is_differentiable(exps[i])) {
            throw_calc_err(exps[i]);
            return NULL;
        }

        // Compute the expression
        Value *v = exps[i]->valueOf(env);
        Value *dv = ((Differentiable*) exps[i])->derivativeOf(x, env, denv);

        if (!v || !dv) {
            // Garbage collection will happen here
            while (i--) {
                env = env->subenvironment();
                denv = denv->subenvironment();
            }
            return NULL;
        }
        
        // Add it to the environment
        env = new ExtendEnv(ids[i], v->clone(), env);
        
        // Now, we will add the derivative to the environment
        denv = new ExtendEnv(ids[i], dv, denv);

        // We permit all lambdas to have recursive behavior
        if (typeid(*v) == typeid(LambdaVal)) {
            lambdas.add(0, (LambdaVal*) v);
        }
    }
    
    // Apply recursive principles
    while (!lambdas.isEmpty()) {
        LambdaVal *v = lambdas.remove(0);
        v->setEnv(env);
    }

    // Compute the result
    Value *y = ((Differentiable*) body)->derivativeOf(x, env, denv);
    
    // Garbage collection
    // Not implemented yet
        
    // Return the result
    return y;


}

Value* ListExp::derivativeOf(string x, Environment *env, Environment *denv) {
    // d/dx [u_0, u_1, ...] = [d/dx u_0, d/dx u_1, ...]
    ListVal *val = new ListVal();
    
    // Add each item
    auto it = list->iterator();
    for(int i = 0; it->hasNext(); i++) {
        // Compute the value of each item
        Expression *exp = it->next();
        if (!is_differentiable(exp)) {
            delete it;
            return NULL;
        }
        Value *v = ((Differentiable*) exp)->derivativeOf(x, env, denv);
        if (!v) {
            delete it;
            return NULL;
        } else
            val->get()->add(i, v);
    }

    delete it;
    return val;
}

Value* ListAccessExp::derivativeOf(string x, Environment *env, Environment *denv) {
    if (!is_differentiable(list)) {
        throw_calc_err(list);
        return NULL;
    }

    return ((Differentiable*) list)->derivativeOf(x, env, denv);

}

Value* MagnitudeExp::derivativeOf(string x, Environment *env, Environment *denv) {
    if (!is_differentiable(exp)) return NULL;

    Value *res = NULL;
    
    Value *v = exp->valueOf(env);

    if (!v) return NULL;
    else if (typeid(*v) == typeid(IntVal) || typeid(*v) == typeid(RealVal)) {
        auto val = (typeid(*v) == typeid(IntVal))
                ? ((IntVal*) v)->get()
                : ((RealVal*) v)->get();

        v->rem_ref();

        Value *dv = ((Differentiable*) exp)->derivativeOf(x, env, denv);
        if (dv) {
            if (typeid(*dv) == typeid(IntVal))
                res = new IntVal((val >= 0 ? 1 : -1) * ((IntVal*) dv)->get());
            else if (typeid(*dv) == typeid(RealVal))
                res = new IntVal((val >= 0 ? 1 : -1) * ((IntVal*) dv)->get());
            else
                throw_err("runtime", "expression '" + v->toString() + "' does not differentiate to numerical type");
        }

        dv->rem_ref();
    } else {
        throw_err("runtime", "expression '" + v->toString() + "' is not of numerical type");
        v->rem_ref();
    }

    return res;
}

Value* MapExp::derivativeOf(string x, Environment *env, Environment *denv) {
    if (!is_differentiable(list)) return NULL;
    if (!is_differentiable(func)) return NULL;

    Value *vs = list->valueOf(env);
    if (!vs) return NULL;

    Value *dvs = ((Differentiable*) list)->derivativeOf(x, env, denv);
    if (!dvs) {
        // The list could not be differentiated
        vs->rem_ref();
        return NULL;
    }

    Value *f = ((Differentiable*) func)->derivativeOf(x, env, denv);
    if (!f) {
        vs->rem_ref();
        dvs->rem_ref();
        return NULL;
    }

    if (typeid(*f) != typeid(LambdaVal)) {
        throw_type_err(func, "lambda");
        vs->rem_ref();
        dvs->rem_ref();
        f->rem_ref();
        return NULL;
    }

    // Extract the function... require that it have one argument
    LambdaVal *fn = (LambdaVal*) f;
    if (fn->getArgs()[0] == "" || fn->getArgs()[1] != "") {
        throw_err("runtime", "map function '" + fn->toString() + "' does not take exactly one argument");
        fn->rem_ref();
        vs->rem_ref();
        dvs->rem_ref();
        return NULL;
    }

    if (typeid(*vs) == typeid(ListVal)) {
        // Given a list, map each element of the list
        ListVal *vals = (ListVal*) vs;
        ListVal *dvals = (ListVal*) dvs;

        ListVal *res = new ListVal;
        
        auto it = vals->get()->iterator();
        auto dit = dvals->get()->iterator();
        while (it->hasNext()) {
            Value *v = it->next();
            Value *dv = dit->next();

            Value **xs = new Value*[2];
            xs[0] = v;
            xs[1] = NULL;

            Value *elem = fn->apply(xs);

            if (!elem) {
                fn->rem_ref();
                vals->rem_ref();
                dvals->rem_ref();
                res->rem_ref();
                return NULL;
            } else {
                // Compute the other part
                Expression *cell = new MultExp(
                    reexpress(elem), reexpress(dv)
                );
                elem->rem_ref();

                elem = cell->valueOf(env);
                delete cell;

                if (elem)
                    // Compute the answer
                    res->get()->add(res->get()->size(), elem);
                else {
                    // The product could not be computed; collect garbage and quit
                    fn->rem_ref();
                    vals->rem_ref();
                    dvals->rem_ref();
                    res->rem_ref();
                    return NULL;
                }
            }
        }
        delete it;
        delete dit;
        
        // Garbage collection
        fn->rem_ref();
        vals->rem_ref();
        dvals->rem_ref();

        return res;

    } else if (typeid(*vs) == typeid(MatrixVal)) {
        Matrix v = ((MatrixVal*) vs)->get();
        Matrix M(v.R, v.C);

        Value *xs[2];
        xs[1] = NULL;
        
        for (int r = 0; r < M.R; r++)
        for (int c = 0; c < M.C; c++) {
            xs[0] = new RealVal(v(r, c));

            Value *elem = fn->apply(xs);
            
            // Garbage collection
            xs[0]->rem_ref();

            if (!elem) {
                // The matrix is non-computable, so failure!
                fn->rem_ref();
                vs->rem_ref();
                return NULL;
            } else if (typeid(*elem) == typeid(RealVal)) {
                M(r, c) = ((RealVal*) elem)->get();
            } else if (typeid(*elem) == typeid(IntVal)) {
                M(r, c) = (float) (((IntVal*) elem)->get());
            } else {
                elem->rem_ref();
                fn->rem_ref();
                vs->rem_ref();
                return NULL;
            }
        }

        fn->rem_ref();
        vs->rem_ref();
        return new MatrixVal(M);

    } else {
        vs->rem_ref();
        fn->rem_ref();
        return NULL;
    }
}


Value* MatrixExp::derivativeOf(string x, Environment *env, Environment *denv) {
    if (!is_differentiable(list)) return NULL;
    Value *v = ((Differentiable*) list)->derivativeOf(x, env, denv);

    if (!v) return NULL;
    else if (typeid(*v) != typeid(ListVal)) {
        v->rem_ref();
        return NULL;
    }
    ListVal *array = (ListVal*) v;

    if (array->get()->size() == 0) {
        // We will not permit arrays with 0 rows
        v->rem_ref();
        return NULL;
    }

    List<Value*> *arr = array->get();
    int R = array->get()->size();
    int C = 0;
    
    // We must verify that the outcome is authentic
    auto it = arr->iterator();
    for (int i = 0; i < R; i++) {
        v = it->next(); // Get the next "row"
        if (typeid(*v) != typeid(ListVal)) {
            // Ensure that the list is a 2D list
            array->rem_ref();
            delete it;
            return NULL;
        }
        
        List<Value*> *row = ((ListVal*) v)->get();
        if (i && row->size() != C) {
            // Ensure the array is square
            array->rem_ref();
            delete it;
            return NULL;
        } else if (!i) {
            C = ((ListVal*) v)->get()->size();
            if (C == 0) {
                array->rem_ref();
                delete it;
                return NULL;
            }
        }
        
        // Verify that the row only contains numbers
        auto jt = row->iterator();
        while (jt->hasNext()) {
            v = jt->next();
            if (typeid(*v) != typeid(RealVal) && typeid(*v) != typeid(IntVal)) {
                delete it;
                delete jt;
                array->rem_ref();
                return NULL;
            }
        }

    }
    delete it;

    float *vals = new float[R*C];

    int k = 0;
    it = arr->iterator();
    for (int i = 0; i < R; i++) {
        ListVal *rowval = ((ListVal*) it->next());
        List<Value*> *row = rowval->get();

        auto jt = row->iterator();
        for (int j = 0; j < C; j++, k++) {
            Value *f = jt->next();

            if (typeid(*f) == typeid(RealVal)) vals[k] = ((RealVal*) f)->get();
            else if (typeid(*f) == typeid(IntVal)) vals[k] = (float) ((IntVal*) f)->get();
        }
        delete jt;
    }
    delete it;

    array->rem_ref();

    Matrix m(R, C, vals);
    
    v = new MatrixVal(m);

    return v;
}

Value* MultExp::derivativeOf(string x, Environment *env, Environment *denv) { 
    if (!is_differentiable(left)) return NULL;
    if (!is_differentiable(right)) return NULL;


    Expression *a = reexpress(((Differentiable*) left)->derivativeOf(x, env, denv));
    Expression *b = reexpress(((Differentiable*) right)->derivativeOf(x, env, denv));

    if (!a || !b) {
        delete a, b;
        return NULL;
    } else {
        Expression *exp = new SumExp(new MultExp(left->clone(), b), new MultExp(right->clone(), a));
        Value *c = exp->valueOf(env);

        delete exp;
        return c;
    }

}

Value* SetExp::derivativeOf(string x, Environment *env, Environment *denv) {
    Value *v = NULL;
    Value *dv = NULL;

    for (int i = 0; exps[i]; i++) {
        if (!is_differentiable(tgts[i]))
            return NULL;
        if (!is_differentiable(exps[i]))
            return NULL;

        // Get info for modifying the environment
        Value *u = tgts[i]->valueOf(env);
        Value *du = ((Differentiable*) tgts[i])->derivativeOf(x, env, denv);
        if (!u)
            // The variable doesn't exist
            return NULL;

        // Evaluate the expression
        v = exps[i]->valueOf(env);

        dv = ((Differentiable*) exps[i])->derivativeOf(x, env, denv);

        if (!v || !dv)
            // The value could not be evaluated.
            return NULL;
        
        // Set the new value
        if (u->set(v) | du->set(dv))
            return NULL;

    }
    
    // To be simple, we return 0 on completion
    // as opposed to NULL, which indicates a failure.
    return v ? v : new VoidVal;
}

Value* SumExp::derivativeOf(string x, Environment *env, Environment *denv) {
    if (!is_differentiable(left)) return NULL;
    if (!is_differentiable(right)) return NULL;

    Value *a = ((Differentiable*) left)->derivativeOf(x, env, denv);
    if (!a) return NULL;

    Value *b = ((Differentiable*) right)->derivativeOf(x, env, denv);
    if (!b) {
        a->rem_ref();
        return NULL;
    }

    Value *c = op(a, b);
    
    a->rem_ref(); b->rem_ref();
    return c;
}

Value* VarExp::derivativeOf(string x, Environment *env, Environment *denv) {
    // We have a list of derivatives, from which we can easily perform
    // a lookup.
    Value *dv = denv->apply(id);
    if (!dv)
        throw_err("runtime", "derivative of variable '" + id + "' is not known within this context");
    else
        dv->add_ref();

    return dv;
}

Value* WhileExp::derivativeOf(string x, Environment *env, Environment *denv) {
    if (!is_differentiable(body)) return NULL;

    Differentiable *b = (Differentiable*) body;

    bool skip = alwaysEnter;
    Value *v = new VoidVal;

    while (true) {
        Value *c = cond->valueOf(env);
        if (!c) return NULL;
        else if (typeid(*c) != typeid(BoolVal)) {
            throw_type_err(cond, "boolean");
            return NULL;
        } else if (skip || ((BoolVal*) c)->get()) {
            // If do-while was enacted, cease the action.
            skip = false;
            // Compute the new outcome. If it is
            // NULL, computation failed, so NULL
            // should be returned.
            if (!(v = b->derivativeOf(x, env, denv)))
                return NULL;
        } else
            return v;
    }
}


