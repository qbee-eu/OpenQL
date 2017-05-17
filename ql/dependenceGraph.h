/**
 * @file   dependenceGraph.h
 * @date   01/2017
 * @author Imran Ashraf
 * @brief  ASAP/AlAP scheduling
 */

#ifndef DEPENDENCEGRAPH_H
#define DEPENDENCEGRAPH_H

#include <lemon/list_graph.h>
#include <lemon/lgf_reader.h>
#include <lemon/lgf_writer.h>
#include <lemon/dijkstra.h>
#include <lemon/connectivity.h>

#include "gate.h"
#include "circuit.h"

using namespace std;
using namespace lemon;

enum DepTypes{RAW,WAW,WAR};
const string DepTypesNames[] = {"RAW", "WAW", "WAR"};
auto MAX_CYCLE = std::numeric_limits<std::size_t>::max();

class DependGraph
{
private:
    ListDigraph graph;

    ListDigraph::NodeMap<ql::gate*> instruction;
    ListDigraph::NodeMap<string> name;
    ListDigraph::ArcMap<int> weight;
    ListDigraph::ArcMap<int> cause;
    ListDigraph::ArcMap<int> depType;

    ListDigraph::NodeMap<double> dist;
    Path<ListDigraph> p;

    ListDigraph::Node s, t;

public:
    DependGraph(): instruction(graph), name(graph), weight(graph),
        cause(graph), depType(graph), dist(graph) {}

    void Init( ql::circuit& ckt, size_t nqubits)
    {
        size_t nQubits = nqubits;
        // std::cout << "nQubits : " << nQubits << std::endl;

        // add dummy source node
        ListDigraph::Node srcNode = graph.addNode();
        instruction[srcNode] = new ql::nop();
        name[srcNode] = instruction[srcNode]->qasm();
        s=srcNode;

        typedef vector<int> ReadersListType;
        vector<ReadersListType> LastReaders;
        LastReaders.resize(nQubits);

        int srcID = graph.id(srcNode);
        vector<int> LastWriter(nQubits,srcID);

        for( auto ins : ckt )
        {
            // std::cout << "\nCurrent instruction : " << ins->qasm() << std::endl;

            // Add nodes
            ListDigraph::Node consNode = graph.addNode();
            int consID = graph.id(consNode);
            instruction[consNode] = ins;
            name[consNode] = ins->qasm();

            // Add edges
            size_t operandNo=0;
            auto operands = ins->operands;
            for( auto operand : operands )
            {
                // cout << "Operand is " << operand << std::endl;
                int prodID = LastWriter[operand];
                ListDigraph::Node prodNode = graph.nodeFromId(prodID);
                ListDigraph::Arc arc = graph.addArc(prodNode,consNode);
                if(prodID == srcID)
                    weight[arc] = 1; // TODO OR 0 as SOURCE is dummy node?
                else
                    weight[arc] = instruction[prodNode]->latency;

                cause[arc] = operand;
                // Last operand is target, well Mostsly! TODO Fix it for other cases
                if( operandNo == operands.size()-1 ) 
                {
                    depType[arc] = WAW;
                    LastWriter[operand] = consID;

                    // for WAR dependencies
                    ReadersListType readers = LastReaders[operand];
                    for(auto & readerID : readers)
                    {
                        ListDigraph::Node readerNode = graph.nodeFromId(readerID);
                        ListDigraph::Arc arc1 = graph.addArc(readerNode,consNode);
                        if(prodID == srcID)
                            weight[arc1] = 1; // TODO OR 0 as SOURCE is dummy node?
                        else
                            weight[arc1] = instruction[readerNode]->latency;

                        cause[arc1] = operand;
                        depType[arc1] = WAR;
                    }
                }
                else
                {
                    LastReaders[operand].push_back(consID);
                    depType[arc] = RAW;
                }
                operandNo++;
            } // end of operand for
        } // end of instruction for

        // add dummy target node
        ListDigraph::Node targetNode = graph.addNode();
        instruction[targetNode] = new ql::nop();;
        name[targetNode] = instruction[targetNode]->qasm();
        t=targetNode;

        // make links to the dummy target node
        OutDegMap<ListDigraph> outDeg(graph);
        for (ListDigraph::NodeIt n(graph); n != INVALID; ++n)
        {
            if( outDeg[n] == 0 && n!=targetNode )
            {
                ListDigraph::Arc arc = graph.addArc(n,targetNode);
                weight[arc] = 1; // TODO OR 0?
                cause[arc] = 0; // NA
                depType[arc] = RAW; // NA
            }
        }
    }

    void Print()
    {
        std::cout << "Printing Dependence Graph " << std::endl;
        digraphWriter(graph).
        nodeMap("name", name).
        arcMap("cause", cause).
        arcMap("weight", weight).
        // arcMap("depType", depType).
        node("source", s).
        node("target", t).
        run();
    }

    void PrintMatrix()
    {
        std::cout << "Printing Dependence Graph as Matrix" << std::endl;
        ofstream fout;
        // OpenOutFile("dependenceMatrix.dat",fout);
        fout.open( "dependenceMatrix.dat", ios::binary);
        if ( fout.fail() )
        {
            std::cout << "Error opening file" << std::endl;
            return;
        }

        size_t totalInstructions = countNodes(graph);
        vector< vector<bool> > Matrix(totalInstructions, vector<bool>(totalInstructions));

        // now print the edges
        for (ListDigraph::ArcIt arc(graph); arc != INVALID; ++arc)
        {
            ListDigraph::Node srcNode = graph.source(arc);
            ListDigraph::Node dstNode = graph.target(arc);
            size_t srcID = graph.id( srcNode );
            size_t dstID = graph.id( dstNode );
            Matrix[srcID][dstID] = true;
        }

        for(size_t i=1; i<totalInstructions-1;i++)
        {
            for(size_t j=1; j<totalInstructions-1;j++)
            {
                fout << Matrix[j][i] << "\t";
            }
            fout << endl;
        }

        fout.close();
    }

    void PrintDot1_(
                bool WithCritical,
                bool WithCycles,
                ListDigraph::NodeMap<size_t> & cycle,
                std::vector<ListDigraph::Node> & order,
                ofstream& dotout
                )
    {
        ListDigraph::ArcMap<bool> isInCritical(graph);
        if(WithCritical)
        {
            for (ListDigraph::ArcIt a(graph); a != INVALID; ++a)
            {
                isInCritical[a] = false;
                for ( Path<ListDigraph>::ArcIt ap(p); ap != INVALID; ++ap )
                {
                    if(a==ap)
                    {
                        isInCritical[a] = true;
                        break;
                    }
                }
            }
        }

        string NodeStyle(" fontcolor=black, style=filled, fontsize=16");
        string EdgeStyle1(" color=black");
        string EdgeStyle2(" color=red");
        string EdgeStyle = EdgeStyle1;

        dotout << "digraph {\ngraph [ rankdir=TD; ]; // or rankdir=LR"
            << "\nedge [fontsize=16, arrowhead=vee, arrowsize=0.5];"
            << endl;

        // first print the nodes
        for (ListDigraph::NodeIt n(graph); n != INVALID; ++n)
        {
            int nid = graph.id(n);
            string nodeName = name[n];
            dotout  << "\"" << nid << "\""
                    << " [label=\" " << nodeName <<" \""
                    << NodeStyle
                    << "];" << endl;
        }

        if( WithCycles)
        {
            // Print cycle numbers as timeline, as shown below
            size_t cn=0,TotalCycles=0;
            dotout << "{\nnode [shape=plaintext, fontsize=16, fontcolor=blue]; \n";
            ListDigraph::NodeMap<size_t>::MapIt it(cycle);
            if(it != INVALID)
                TotalCycles=cycle[it];
            for(cn=0;cn<=TotalCycles;++cn)
            {
                if(cn>0)
                    dotout << " -> ";
                dotout << "Cycle" << cn;
            }
            dotout << ";\n}\n";

            // Now print ranks, as shown below
            std::vector<ListDigraph::Node>::reverse_iterator rit;
            for ( rit = order.rbegin(); rit != order.rend(); ++rit)
            {
                int nid = graph.id(*rit);
                dotout << "{ rank=same; Cycle" << cycle[*rit] <<"; " <<nid<<"; }\n";
            }
        }

        // now print the edges
        for (ListDigraph::ArcIt arc(graph); arc != INVALID; ++arc)
        {
            ListDigraph::Node srcNode = graph.source(arc);
            ListDigraph::Node dstNode = graph.target(arc);
            int srcID = graph.id( srcNode );
            int dstID = graph.id( dstNode );

            if(WithCritical)
                EdgeStyle = ( isInCritical[arc]==true ) ? EdgeStyle2 : EdgeStyle1;

            dotout << dec
                << "\"" << srcID << "\""
                << "->"
                << "\"" << dstID << "\""
                << "[ label=\""
                << "q" << cause[arc]
                // << " , " << weight[arc]
                // << " , " << DepTypesNames[ depType[arc] ]
                <<"\""
                << " " << EdgeStyle << " "
                << "]"
                << endl;
        }

        dotout << "}" << endl;
    }

    void PrintDot()
    {
        std::cout << "Printing Dependence Graph in DOT" << std::endl;
        ofstream dotout;
        // OpenOutFile("dependenceGraph.dot",dotout);
        dotout.open( "dependenceGraph.dot", ios::binary);
        if ( dotout.fail() )
        {
            std::cout << "Error opening file" << std::endl;
            return;
        }

        ListDigraph::NodeMap<size_t> cycle(graph);
        std::vector<ListDigraph::Node> order;
        PrintDot1_(false, false, cycle, order, dotout);
        dotout.close();
    }

    void TopologicalSort(std::vector<ListDigraph::Node> & order)
    {
        // std::cout << "Performing Topological sort." << std::endl;
        ListDigraph::NodeMap<int> rorder(graph);
        if( !dag(graph) )
            std::cout << "This digraph is not a DAG." << std::endl;

        topologicalSort(graph, rorder);

#ifdef DEBUG
        for (ListDigraph::ArcIt a(graph); a != INVALID; ++a)
        {
            if( rorder[graph.source(a)] > rorder[graph.target(a)] )
                std::cout << "Wrong topologicalSort()" << std::endl;
        }
#endif

        for ( ListDigraph::NodeMap<int>::MapIt it(rorder); it != INVALID; ++it)
        {
            order.push_back(it);
        }
    }

    void PrintTopologicalOrder()
    {
        std::vector<ListDigraph::Node> order;
        TopologicalSort(order);

        std::cout << "Printing nodes in Topological order" << std::endl;
        for ( std::vector<ListDigraph::Node>::reverse_iterator it = order.rbegin(); it != order.rend(); ++it)
        {
            std::cout << name[*it] << std::endl;
        }
    }

    void ScheduleASAP(ListDigraph::NodeMap<size_t> & cycle, std::vector<ListDigraph::Node> & order)
    {
        std::cout << "Performing ASAP Scheduling" << std::endl;
        TopologicalSort(order);

        std::vector<ListDigraph::Node>::reverse_iterator currNode = order.rbegin();
        cycle[*currNode]=0; // src dummy in cycle 0
        ++currNode;
        while(currNode != order.rend() )
        {
            // std::cout << "Scheduling " << name[*currNode] << std::endl;
            size_t currCycle=0;
            for( ListDigraph::InArcIt arc(graph,*currNode); arc != INVALID; ++arc )
            {
                ListDigraph::Node srcNode  = graph.source(arc);
                size_t srcCycle = cycle[srcNode];
                if(currCycle <= srcCycle)
                {
                    currCycle = srcCycle + weight[arc];
                }
            }
            cycle[*currNode]=currCycle;
            ++currNode;
        }
    }

    void PrintScheduleASAP()
    {
        ListDigraph::NodeMap<size_t> cycle(graph);
        std::vector<ListDigraph::Node> order;
        ScheduleASAP(cycle,order);

        std::cout << "\nPrinting ASAP Schedule" << std::endl;
        std::cout << "Cycle <- Instruction " << std::endl;
        std::vector<ListDigraph::Node>::reverse_iterator it;
        for ( it = order.rbegin(); it != order.rend(); ++it)
        {
            std::cout << cycle[*it] << "     <- " <<  name[*it] << std::endl;
        }
    }

    void PrintDotScheduleASAP()
    {
        std::cout << "Printing Scheduled Graph in scheduledASAP.dot" << std::endl;
        ofstream dotout;
        dotout.open( "scheduledASAP.dot", ios::binary);
        if ( dotout.fail() )
        {
            std::cout << "Error opening file" << std::endl;
            return;
        }

        ListDigraph::NodeMap<size_t> cycle(graph);
        std::vector<ListDigraph::Node> order;
        ScheduleASAP(cycle,order);
        PrintDot1_(false,true,cycle,order,dotout);

        dotout.close();
    }

    void PrintQASMScheduledASAP()
    {
        ofstream fout;
        fout.open( "scheduledASAP.qc", ios::binary);
        if ( fout.fail() )
        {
            std::cout << "Error opening file" << std::endl;
            return;
        }

        ListDigraph::NodeMap<size_t> cycle(graph);
        std::vector<ListDigraph::Node> order;
        ScheduleASAP(cycle,order);
        std::cout << "Printing Scheduled QASM in scheduledASAP.qc" << std::endl;

        typedef std::vector<std::string> insInOneCycle;
        std::map<size_t,insInOneCycle> insInAllCycles;

        std::vector<ListDigraph::Node>::reverse_iterator it;
        for ( it = order.rbegin(); it != order.rend(); ++it)
        {
            insInAllCycles[ cycle[*it] ].push_back( name[*it] );
        }

        size_t TotalCycles = 0;
        if( ! order.empty() )
        {
            TotalCycles =  cycle[ *( order.begin() ) ];
        }

        for(size_t currCycle = 1; currCycle<TotalCycles; ++currCycle)
        {
            auto it = insInAllCycles.find(currCycle);
            if( it != insInAllCycles.end() )
            {
                auto nInsThisCycle = insInAllCycles[currCycle].size();
                for(size_t i=0; i<nInsThisCycle; ++i )
                {
                    fout << insInAllCycles[currCycle][i];
                    if( i != nInsThisCycle - 1 ) // last instruction
                        fout << " | ";
                }
            }
            else
            {
                fout << "   nop";
            }
            fout << endl;
        }

        fout.close();
    }

    void ScheduleALAP(ListDigraph::NodeMap<size_t> & cycle, std::vector<ListDigraph::Node> & order)
    {
        std::cout << "Performing ALAP Scheduling" << std::endl;
        TopologicalSort(order);

        std::vector<ListDigraph::Node>::iterator currNode = order.begin();
        cycle[*currNode]=MAX_CYCLE; // src dummy in cycle 0
        ++currNode;
        while(currNode != order.end() )
        {
            // std::cout << "Scheduling " << name[*currNode] << std::endl;
            size_t currCycle=MAX_CYCLE;
            for( ListDigraph::OutArcIt arc(graph,*currNode); arc != INVALID; ++arc )
            {
                ListDigraph::Node targetNode  = graph.target(arc);
                size_t targetCycle = cycle[targetNode];
                if(currCycle >= targetCycle)
                {
                    currCycle = targetCycle - + weight[arc];
                }
            }
            cycle[*currNode]=currCycle;
            ++currNode;
        }
    }

    void PrintScheduleALAP()
    {
        ListDigraph::NodeMap<size_t> cycle(graph);
        std::vector<ListDigraph::Node> order;
        ScheduleALAP(cycle,order);

        std::cout << "\nPrinting ALAP Schedule" << std::endl;
        std::cout << "Cycle <- Instruction " << std::endl;
        std::vector<ListDigraph::Node>::reverse_iterator it;
        for ( it = order.rbegin(); it != order.rend(); ++it)
        {
            std::cout << MAX_CYCLE-cycle[*it] << "     <- " <<  name[*it] << std::endl;
        }
    }

    void PrintDot2_(
                bool WithCritical,
                bool WithCycles,
                ListDigraph::NodeMap<size_t> & cycle,
                std::vector<ListDigraph::Node> & order,
                ofstream& dotout
                )
    {
        ListDigraph::ArcMap<bool> isInCritical(graph);
        if(WithCritical)
        {
            for (ListDigraph::ArcIt a(graph); a != INVALID; ++a)
            {
                isInCritical[a] = false;
                for ( Path<ListDigraph>::ArcIt ap(p); ap != INVALID; ++ap )
                {
                    if(a==ap)
                    {
                        isInCritical[a] = true;
                        break;
                    }
                }
            }
        }

        string NodeStyle(" fontcolor=black, style=filled, fontsize=16");
        string EdgeStyle1(" color=black");
        string EdgeStyle2(" color=red");
        string EdgeStyle = EdgeStyle1;

        dotout << "digraph {\ngraph [ rankdir=TD; ]; // or rankdir=LR"
            << "\nedge [fontsize=16, arrowhead=vee, arrowsize=0.5];"
            << endl;

        // first print the nodes
        for (ListDigraph::NodeIt n(graph); n != INVALID; ++n)
        {
            int nid = graph.id(n);
            string nodeName = name[n];
            dotout  << "\"" << nid << "\""
                    << " [label=\" " << nodeName <<" \""
                    << NodeStyle
                    << "];" << endl;
        }

        if( WithCycles)
        {
            // Print cycle numbers as timeline, as shown below
            size_t cn=0,TotalCycles = MAX_CYCLE - cycle[ *( order.rbegin() ) ];
            dotout << "{\nnode [shape=plaintext, fontsize=16, fontcolor=blue]; \n";

            for(cn=0;cn<=TotalCycles;++cn)
            {
                if(cn>0)
                    dotout << " -> ";
                dotout << "Cycle" << cn;
            }
            dotout << ";\n}\n";

            // Now print ranks, as shown below
            std::vector<ListDigraph::Node>::reverse_iterator rit;
            for ( rit = order.rbegin(); rit != order.rend(); ++rit)
            {
                int nid = graph.id(*rit);
                dotout << "{ rank=same; Cycle" << TotalCycles - (MAX_CYCLE - cycle[*rit]) <<"; " <<nid<<"; }\n";
            }
        }

        // now print the edges
        for (ListDigraph::ArcIt arc(graph); arc != INVALID; ++arc)
        {
            ListDigraph::Node srcNode = graph.source(arc);
            ListDigraph::Node dstNode = graph.target(arc);
            int srcID = graph.id( srcNode );
            int dstID = graph.id( dstNode );

            if(WithCritical)
                EdgeStyle = ( isInCritical[arc]==true ) ? EdgeStyle2 : EdgeStyle1;

            dotout << dec
                << "\"" << srcID << "\""
                << "->"
                << "\"" << dstID << "\""
                << "[ label=\""
                << "q" << cause[arc]
                // << " , " << weight[arc]
                // << " , " << DepTypesNames[ depType[arc] ]
                <<"\""
                << " " << EdgeStyle << " "
                << "]"
                << endl;
        }

        dotout << "}" << endl;
    }

    void PrintDotScheduleALAP()
    {
        std::cout << "Printing Scheduled Graph in scheduledALAP.dot" << std::endl;
        ofstream dotout;
        dotout.open( "scheduledALAP.dot", ios::binary);
        if ( dotout.fail() )
        {
            std::cout << "Error opening file" << std::endl;
            return;
        }

        ListDigraph::NodeMap<size_t> cycle(graph);
        std::vector<ListDigraph::Node> order;
        ScheduleALAP(cycle,order);
        PrintDot2_(false,true,cycle,order,dotout);

        dotout.close();
    }

    void PrintQASMScheduledALAP()
    {
        ofstream fout;
        fout.open( "scheduledALAP.qc", ios::binary);
        if ( fout.fail() )
        {
            std::cout << "Error opening file" << std::endl;
            return;
        }

        ListDigraph::NodeMap<size_t> cycle(graph);
        std::vector<ListDigraph::Node> order;
        ScheduleALAP(cycle,order);
        std::cout << "Printing Scheduled QASM in scheduledALAP.qc" << std::endl;

        typedef std::vector<std::string> insInOneCycle;
        std::map<size_t,insInOneCycle> insInAllCycles;

        std::vector<ListDigraph::Node>::iterator it;
        for ( it = order.begin(); it != order.end(); ++it)
        {
            insInAllCycles[ MAX_CYCLE - cycle[*it] ].push_back( name[*it] );
        }

        size_t TotalCycles = 0;
        if( ! order.empty() )
        {
            TotalCycles =  MAX_CYCLE - cycle[ *( order.rbegin() ) ];
        }

        for(size_t currCycle = TotalCycles-1; currCycle>0; --currCycle)
        {
            auto it = insInAllCycles.find(currCycle);
            if( it != insInAllCycles.end() )
            {
                auto nInsThisCycle = insInAllCycles[currCycle].size();
                for(size_t i=0; i<nInsThisCycle; ++i )
                {
                    fout << insInAllCycles[currCycle][i];
                    if( i != nInsThisCycle - 1 ) // last instruction
                        fout << " | ";
                }
            }
            else
            {
                fout << "   nop";
            }
            fout << endl;
        }
    }

};

#endif
