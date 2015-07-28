/*
# Licensed Materials - Property of IBM
# Copyright IBM Corp. 2015  
 */
package com.ibm.streamsx.topology.internal.functional.ops;

import static com.ibm.streamsx.topology.internal.functional.FunctionalHelper.getLogicObject;
import static com.ibm.streamsx.topology.internal.functional.FunctionalHelper.getOutputMapping;

import com.ibm.streams.operator.OperatorContext;
import com.ibm.streams.operator.OutputTuple;
import com.ibm.streams.operator.StreamingOutput;
import com.ibm.streams.operator.model.OutputPortSet;
import com.ibm.streams.operator.model.Parameter;
import com.ibm.streams.operator.model.PrimitiveOperator;
import com.ibm.streams.operator.model.SharedLoader;
import com.ibm.streams.operator.samples.patterns.PollingTupleProducer;
import com.ibm.streamsx.topology.function.Supplier;
import com.ibm.streamsx.topology.internal.functional.FunctionalHelper;
import com.ibm.streamsx.topology.internal.spljava.SPLMapping;

@PrimitiveOperator
@OutputPortSet(cardinality = 1)
@SharedLoader
public class FunctionPeriodicSource extends PollingTupleProducer implements Functional {

    private Supplier<Iterable<Object>> data;
    private SPLMapping<Object> mapping;

    private String functionalLogic;
    private String[] jar;
    private StreamingOutput<OutputTuple> output;

    @Override
    public synchronized void initialize(OperatorContext context)
            throws Exception {
        super.initialize(context);

        FunctionalHelper.addLibraries(this, getJar());

        data = getLogicObject(getFunctionalLogic());
        output = getOutput(0);
        mapping = getOutputMapping(this, 0);
    }

    public String getFunctionalLogic() {
        return functionalLogic;
    }

    @Parameter
    public void setFunctionalLogic(String logic) {
        this.functionalLogic = logic;
    }

    public String[] getJar() {
        return jar;
    }

    @Parameter(optional = true)
    public void setJar(String[] jar) {
        this.jar = jar;
    }
    
    @Override
    protected void fetchTuples() throws Exception {

        for (Object tuple : data.get()) {
            if (Thread.interrupted())
                return;
            if (tuple == null)
                continue;
            output.submit(mapping.convertTo(tuple));
        }
    }
    
    @Override
    public void shutdown() throws Exception {
        FunctionFunctor.closeLogic(data);
        super.shutdown();
    }
}