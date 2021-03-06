/* ----------------------------------------------------------------------- *//**
 *
 * @file PGMain.cpp
 *
 * @brief PostgreSQL database abstraction layer 
 *
 *//* -------------------------------------------------------------------- *//**
 *
 * Postgres is a platform where the C interface supports reflection, so all we
 * need to do is to include the PostgreSQL database abstraction layer and the
 * default declarations.
 *
 * @par Platform notes
 * We can be sure that all code in the core library will use our overloads:
 * - With the normal POSIX linking (e.g., on Linux), there is only one namespace
 *   for symbols. Since the connector library is loaded before the core library.
 * - On Mac OS X, operator new and delete are exempt from the usual two-level
 *   namespace. See:
 *   http://developer.apple.com/library/mac/#documentation/DeveloperTools/Conceptual/CppRuntimeEnv/Articles/LibCPPDeployment.html
 *   We can therefore be sure that all code in the core library will use our
 *   overloads.
 * - FIXME: Check!
 *   On Solaris, we use direct binding in general, but mark
 *   <tt>operator new</tt> and <tt>delete</tt> as exempt.
 *   http://download.oracle.com/docs/cd/E19253-01/817-1984/aehzq/index.html
 */

#include <dbconnector/PGCommon.hpp>
#include <dbconnector/PGToDatumConverter.hpp>
#include <dbconnector/PGInterface.hpp>
#include <dbconnector/PGType.hpp>

#include <modules/modules.hpp>

extern "C" {
    #include <funcapi.h>
    #include <utils/builtins.h>    // needed for format_procedure()
    #include <executor/executor.h> // Greenplum requires this for GetAttributeByNum()
} // extern "C"

namespace madlib {

namespace dbconnector {

/**
 * @brief C++ entry point for calls from the database
 *
 * The DBMS calls an export "C" function defined in PGMain.cpp, which calls
 * this function.
 */
inline static Datum call(MADFunction &f, PG_FUNCTION_ARGS) {
    int sqlerrcode;
    char msg[2048];

    try {
        PGInterface db(fcinfo);
        
        try {
            PGType<FunctionCallInfo> arg(fcinfo);
            AnyType result = f(db,
                    AnyType(
                        AbstractTypeSPtr(
                            &arg,
                            NoDeleter<AbstractType>()
                        )
                    ));

            if (result.isNull())
                PG_RETURN_NULL();
            
            return PGToDatumConverter(fcinfo).convertToDatum(result);
        } catch (std::bad_alloc &) {
            sqlerrcode = ERRCODE_OUT_OF_MEMORY;
            strncpy(msg,
                "Memory allocation failed. Typically, this indicates that "
                PACKAGE_NAME
                " limits the available memory to less than what is needed for this "
                "input.",
                sizeof(msg));
        } catch (std::exception &exc) {
            sqlerrcode = ERRCODE_INVALID_PARAMETER_VALUE;
            const char *error = exc.what();
            
            // If there is a pending error, we report this instead.
            if (db.lastError())
                error = db.lastError();
            
            strncpy(msg, error, sizeof(msg));
        }
    } catch (std::exception &exc) {
        sqlerrcode = ERRCODE_INVALID_PARAMETER_VALUE;
        strncpy(msg, exc.what(), sizeof(msg));
    } catch (...) {
        sqlerrcode = ERRCODE_INVALID_PARAMETER_VALUE;
        strncpy(msg,
            "Unknown exception was raised.",
            sizeof(msg));
    }
    
    // This code will only be reached in case of error.
    // We want to ereport only here, with only POD (plain old data) left on the
    // stack. (ereport will do a longjmp)
    msg[sizeof(msg) - 1] = '\0';
    ereport (
        ERROR, (
            errcode(sqlerrcode),
            errmsg(
                "Function \"%s\": %s",
                format_procedure(fcinfo->flinfo->fn_oid),
                msg
            )
        )
    );
    
    // This will never be reached.
    PG_RETURN_NULL();
}

extern "C" {
    PG_MODULE_MAGIC;
} // extern "C"

#define DECLARE_UDF(NameSpace, Function) DECLARE_UDF_EXT(Function, NameSpace, Function)

#define DECLARE_UDF_EXT(SQLName, NameSpace, Function) \
    extern "C" { \
        Datum SQLName(PG_FUNCTION_ARGS); \
        PG_FUNCTION_INFO_V1(SQLName); \
        Datum SQLName(PG_FUNCTION_ARGS) { \
            return call( \
                modules::NameSpace::Function, \
                fcinfo); \
        } \
    }

#include <modules/declarations.hpp>

#undef DECLARE_UDF_EXT
#undef DECLARE_UDF

} // namespace dbconnector

} // namespace madlib
