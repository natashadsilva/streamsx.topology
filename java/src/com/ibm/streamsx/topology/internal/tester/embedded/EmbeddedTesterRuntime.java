/*
# Licensed Materials - Property of IBM
# Copyright IBM Corp. 2017  
 */
package com.ibm.streamsx.topology.internal.tester.embedded;

import static java.util.concurrent.TimeUnit.MILLISECONDS;
import static java.util.concurrent.TimeUnit.SECONDS;

import java.util.Map;
import java.util.Set;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

import com.ibm.streams.flow.declare.OutputPortDeclaration;
import com.ibm.streams.flow.handlers.StreamHandler;
import com.ibm.streams.flow.javaprimitives.JavaOperatorTester;
import com.ibm.streams.flow.javaprimitives.JavaTestableGraph;
import com.ibm.streams.operator.Tuple;
import com.ibm.streamsx.topology.TStream;
import com.ibm.streamsx.topology.builder.BOutput;
import com.ibm.streamsx.topology.builder.BOutputPort;
import com.ibm.streamsx.topology.context.StreamsContext;
import com.ibm.streamsx.topology.internal.embedded.EmbeddedGraph;
import com.ibm.streamsx.topology.internal.tester.ConditionTesterImpl;
import com.ibm.streamsx.topology.internal.tester.conditions.handlers.HandlerTesterRuntime;
import com.ibm.streamsx.topology.tester.Condition;

/**
 * Embedded tester that takes the conditions and
 * adds corresponding handlers directly into the
 * application graph.
 *
 */
public final class EmbeddedTesterRuntime extends HandlerTesterRuntime {
    
    public EmbeddedTesterRuntime(ConditionTesterImpl tester) {
        super(tester);
    }

    @Override
    public void start(Object info) throws Exception {
        EmbeddedGraph eg = (EmbeddedGraph) info;
        
        setupEmbeddedTestHandlers(eg);
    }

    @Override
    public void shutdown(Future<?> future) throws Exception {
        future.cancel(true);
    }
    
    private void setupEmbeddedTestHandlers(EmbeddedGraph eg) throws Exception {
        
        final JavaTestableGraph tg = eg.getExecutionGraph();

        for (TStream<?> stream : handlers.keySet()) {
            Set<StreamHandler<Tuple>> streamHandlers = handlers.get(stream);

            final BOutput output = stream.output();
            if (output instanceof BOutputPort) {
                final BOutputPort outputPort = (BOutputPort) output;
                final OutputPortDeclaration portDecl = eg.getOutputPort(outputPort.name());
                
                // final OutputPortDeclaration portDecl = outputPort.port();
                for (StreamHandler<Tuple> streamHandler : streamHandlers) {
                    tg.registerStreamHandler(portDecl, streamHandler);
                }
            }
        }
    }
    
    @Override
    public TestState checkTestState(StreamsContext<?> context, Map<String, Object> config, Future<?> future,
            Condition<?> endCondition) throws Exception {

        try {
            future.get(200, MILLISECONDS);
            return this.testStateFromConditions(true, true);
        } catch (TimeoutException e) {
            return TestState.NO_PROGRESS;
        }
    }
}
