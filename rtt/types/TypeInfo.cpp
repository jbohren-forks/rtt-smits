/***************************************************************************
  tag: The SourceWorks  Tue Sep 7 00:55:18 CEST 2010  TypeInfo.cpp

                        TypeInfo.cpp -  description
                           -------------------
    begin                : Tue September 07 2010
    copyright            : (C) 2010 The SourceWorks
    email                : peter@thesourceworks.com

 ***************************************************************************
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU General Public                   *
 *   License as published by the Free Software Foundation;                 *
 *   version 2 of the License.                                             *
 *                                                                         *
 *   As a special exception, you may use this file as part of a free       *
 *   software library without restriction.  Specifically, if other files   *
 *   instantiate templates or use macros or inline functions from this     *
 *   file, or you compile this file and link it with other files to        *
 *   produce an executable, this file does not by itself cause the         *
 *   resulting executable to be covered by the GNU General Public          *
 *   License.  This exception does not however invalidate any other        *
 *   reasons why the executable file might be covered by the GNU General   *
 *   Public License.                                                       *
 *                                                                         *
 *   This library is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU General Public             *
 *   License along with this library; if not, write to the Free Software   *
 *   Foundation, Inc., 59 Temple Place,                                    *
 *   Suite 330, Boston, MA  02111-1307  USA                                *
 *                                                                         *
 ***************************************************************************/


#include "TypeInfo.hpp"
#include "TypeBuilder.hpp"
#include "../internal/DataSourceTypeInfo.hpp"
#include "TypeTransporter.hpp"

#include "rtt-config.h"

#include "../Logger.hpp"
#include "../base/AttributeBase.hpp"

#ifdef OS_HAVE_STREAMS
#include <sstream>
#endif

namespace RTT
{
    using namespace std;
    using namespace detail;
    using namespace internal;

    AttributeBase* TypeInfo::buildVariable(std::string name, int ) const {
        return this->buildVariable(name);
    }

    AttributeBase* TypeInfo::buildConstant(std::string name,DataSourceBase::shared_ptr dsb, int ) const {
        return this->buildConstant(name, dsb );
    }


    TypeInfo::~TypeInfo()
    {
        // cleanup transporters
        for (Transporters::iterator i = transporters.begin(); i != transporters.end(); ++i)
            delete *i;

        // cleanup constructors
        for (Constructors::iterator i= constructors.begin(); i != constructors.end(); ++i)
            delete (*i);
    }

    DataSourceBase::shared_ptr TypeInfo::construct(const std::vector<DataSourceBase::shared_ptr>& args) const
    {

        DataSourceBase::shared_ptr ds;
        // build default constructible:
        if ( args.empty() ) {
            AttributeBase* ab = this->buildVariable("constructor");
            ds = ab->getDataSource();
            delete ab;
            return ds;
        }

        // return same type if equal:
        if ( args.size() == 1 && args.front()->getTypeInfo() == this )
            return args.front();

        Constructors::const_iterator i= constructors.begin();
        while (i != constructors.end() ) {
            ds = (*i)->build( args );
            if ( ds )
                return ds;
            ++i;
        }
        // returns empty data source to indicate not constructible
        return ds;
    }

    void TypeInfo::addConstructor(TypeBuilder* tb) {
        constructors.push_back(tb);
    }

    DataSourceBase::shared_ptr TypeInfo::convert(DataSourceBase::shared_ptr arg) const
    {
        DataSourceBase::shared_ptr ds;
        Constructors::const_iterator i= constructors.begin();
        if ( arg->getTypeInfo() == this )
            return arg;
        //log(Info) << getTypeName() << ": trying to convert from " << arg->getTypeName()<<endlog();
        while (i != constructors.end() ) {
            ds = (*i)->convert( arg );
            if ( ds ) {
                return ds;
            }
            ++i;
        }
        // if no conversion happend, return arg again.
        return arg;
    }

    base::DataSourceBase::shared_ptr TypeInfo::decomposeType(base::DataSourceBase::shared_ptr source) const
    {
        // return deprecated api in case user did not implement this.
        return convertType(source);
    }

    base::DataSourceBase::shared_ptr TypeInfo::convertType(base::DataSourceBase::shared_ptr source) const
    {
        return base::DataSourceBase::shared_ptr();
    }

    bool TypeInfo::resize(base::DataSourceBase::shared_ptr arg, int size) const {
        return false;
    }

    string TypeInfo::toString( DataSourceBase::shared_ptr in ) const
    {
#ifdef OS_HAVE_STREAMS
        stringstream result;
        this->write( result, in );
        return result.str();
#else
        return string("(") + in->getTypeInfo()->getTypeName() + ")";
#endif
    }

    bool TypeInfo::fromString( const std::string& value, DataSourceBase::shared_ptr out ) const
    {
        stringstream result(value);
        return this->read( result, out ).good();
    }

    bool TypeInfo::addProtocol(int protocol_id, TypeTransporter* tt)
    {
        if (transporters.size() < static_cast<size_t>(protocol_id + 1))
            transporters.resize(protocol_id + 1);
        if ( transporters[protocol_id] ) {
            log(Error) << "A protocol with id "<<protocol_id<<" was already added for type "<< getTypeName()<<endlog();
            return false;
        }
        transporters[protocol_id] = tt;
        return true;
    }

    TypeTransporter* TypeInfo::getProtocol(int protocol_id) const
    {
        // if the protocol is unknown to this type, return the protocol of the 'unknown type'
        // type, which is a fallback such that we won't have to return zero, but can
        // gracefully fall-back.
        // In order to not endlessly recurse, we check if we aren't the UnknownType !
        if ( protocol_id + 1 > int(transporters.size()) || transporters[protocol_id] == 0) {
            if ( DataSourceTypeInfo<UnknownType>::getTypeInfo() != this )
                return DataSourceTypeInfo<UnknownType>::getTypeInfo()->getProtocol( protocol_id );
            else {
                log(Warning) << "The protocol with id "<<protocol_id<<" did not register a fall-back handler for unknown types!"<<endlog();
                log(Warning) << "  triggered by: "<< getTypeName() << " which does not have a transport."<<endlog();
                return 0; // That transport did not register a fall-back !
            }
        }
        return transporters[protocol_id];
    }

    void TypeInfo::migrateProtocols(TypeInfo* orig)
    {
        assert( transporters.empty() );
        transporters.insert(transporters.begin(), orig->transporters.begin(), orig->transporters.end());
        orig->transporters.clear(); // prevents deletion.
    }

    bool TypeInfo::hasProtocol(int protocol_id) const
    {
        // if the protocol is unknown to this type, return the protocol of the 'unknown type'
        // type, which is a fallback such that we won't have to return zero, but can
        // gracefully fall-back.
        // In order to not endlessly recurse, we check if we aren't the UnknownType !
        if ( protocol_id + 1 > int(transporters.size()) || transporters[protocol_id] == 0) {
            return false;
        }
        return true;
    }

    std::vector<int> TypeInfo::getTransportNames() const
    {
        std::vector<int>    ret;
        for (size_t i=0; i<transporters.size(); ++i)
        {
            // dump only protocols with an actual transporter
            // NB the transporter does not have a name, so you have to manually
            // match the protocol number to an actual transport
            if (0 != transporters[i])
            {
                ret.push_back(i);
            }
        }
        return ret;
    }

    vector<string> TypeInfo::getMemberNames() const
    {
        return vector<string>();
    }

    DataSourceBase::shared_ptr TypeInfo::getMember(DataSourceBase::shared_ptr item, const std::string& part_name) const
    {
        /** ** Strong typed data **
         *
         * for( set i = 0; i <=10; set i = i + 1) {
         *      value[i] = i; // sequence index, runtime structure
         *      value.i  = i; // part name, browse static structure
         * }
         * set frame.pos = vector(a,b,c);     // getMember("pos")
         * set frame.pos[3] = vector(a,b,c);  // getMember("pos")->getMember(3)
         * set frame[3].pos = vector(a,b,c);  // getMember(3)->getMember("pos")
         * set frame[i].pos = vector(a,b,c);  // getMember( $i )->getMember("pos")
         * set frame["tool"].pos = vector(a,b,c); // getMember("tool") xx
         * set frame[arg].pos = vector(a,b,c);// getMember( arg )->getMember("pos")
         */
        log(Debug) <<"No parts registered for "<< getTypeName() <<endlog();
        if ( part_name.empty() )
            return item;
        else
            return DataSourceBase::shared_ptr();
    }

    DataSourceBase::shared_ptr TypeInfo::getMember(DataSourceBase::shared_ptr item, DataSourceBase::shared_ptr id) const
    {
        /** ** Strong typed data **
         *
         * for( set i = 0; i <=10; set i = i + 1) {
         *      value[i] = i; // sequence index, runtime structure
         *      value.i  = i; // part name, browse static structure
         * }
         * set frame.pos = vector(a,b,c);     // getMember("pos")
         * set frame.pos[3] = vector(a,b,c);  // getMember("pos")->getMember(3)
         * set frame[3].pos = vector(a,b,c);  // getMember(3)->getMember("pos")
         * set frame[i].pos = vector(a,b,c);  // getMember( $i )->getMember("pos")
         * set frame["tool"].pos = vector(a,b,c); // getMember("tool") xx
         * set frame[arg].pos = vector(a,b,c);// getMember( arg )->getMember("pos")
         */
        log(Debug) <<"No parts registered for "<< getTypeName() <<endlog();
        return DataSourceBase::shared_ptr();
    }
}
