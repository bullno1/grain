#ifndef GRAINC_GRAPH_HPP
#define GRAINC_GRAPH_HPP

#include <set>

template<typename T>
class Graph
{
public:
	typedef T Node;
	typedef std::pair<T, T> Edge;
	typedef std::set<Edge> EdgeSet;
	typedef std::set<Node> NodeSet;

	void addEdge(const Node& from, const Node& to)
	{
		mEdges.insert(std::make_pair(from, to));
		addNode(from);
		addNode(to);
	}

	void addNode(const Node& node)
	{
		mNodes.insert(node);
	}

	void removeNode(const Node& node)
	{
		mNodes.erase(node);
		bool done;
		do
		{
			done = true;
			for(typename EdgeSet::const_iterator edgeItr = mEdges.begin(); edgeItr != mEdges.end(); ++edgeItr)
			{
				if(edgeItr->first == node || edgeItr->second == node)
				{
					mEdges.erase(edgeItr);
					done = false;
					break;
				}
			}
		} while(!done);
	}

	void findRootNodes(NodeSet& out) const
	{
		for(typename NodeSet::const_iterator nodeItr = mNodes.begin(); nodeItr != mNodes.end(); ++nodeItr)
		{
			bool isRoot = true;

			for(typename EdgeSet::const_iterator edgeItr = mEdges.begin(); edgeItr != mEdges.end(); ++edgeItr)
			{
				if(edgeItr->second == *nodeItr)
				{
					isRoot = false;
					break;
				}
			}

			if(isRoot)
			{
				out.insert(*nodeItr);
			}
		}
	}

	bool empty() const
	{
		return mEdges.empty() && mNodes.epmty();
	}
private:
	EdgeSet mEdges;
	NodeSet mNodes;
};

#endif
