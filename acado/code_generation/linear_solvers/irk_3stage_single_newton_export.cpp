/*
 *    This file is part of ACADO Toolkit.
 *
 *    ACADO Toolkit -- A Toolkit for Automatic Control and Dynamic Optimization.
 *    Copyright (C) 2008-2014 by Boris Houska, Hans Joachim Ferreau,
 *    Milan Vukov, Rien Quirynen, KU Leuven.
 *    Developed within the Optimization in Engineering Center (OPTEC)
 *    under supervision of Moritz Diehl. All rights reserved.
 *
 *    ACADO Toolkit is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU Lesser General Public
 *    License as published by the Free Software Foundation; either
 *    version 3 of the License, or (at your option) any later version.
 *
 *    ACADO Toolkit is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with ACADO Toolkit; if not, write to the Free Software
 *    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */



/**
 *    \file src/code_generation/linear_solvers/irk_3stage_single_newton_export.cpp
 *    \author Rien Quirynen
 *    \date 2015
 */

#include <acado/code_generation/linear_solvers/irk_3stage_single_newton_export.hpp>

using namespace std;

BEGIN_NAMESPACE_ACADO

//
// PUBLIC MEMBER FUNCTIONS:
//

ExportIRK3StageSingleNewton::ExportIRK3StageSingleNewton( UserInteraction* _userInteraction,
									const std::string& _commonHeaderName
									) : ExportGaussElim( _userInteraction,_commonHeaderName )
{
	stepsize = 0;
	implicit = false;
	tau = 0;
}

ExportIRK3StageSingleNewton::~ExportIRK3StageSingleNewton( )
{}

returnValue ExportIRK3StageSingleNewton::getDataDeclarations(	ExportStatementBlock& declarations,
														ExportStruct dataStruct
														) const
{
	ExportGaussElim::getDataDeclarations( declarations, dataStruct );

	declarations.addDeclaration( A_mem,dataStruct );
	declarations.addDeclaration( b_mem,dataStruct );

	return SUCCESSFUL_RETURN;
}


returnValue ExportIRK3StageSingleNewton::getFunctionDeclarations(	ExportStatementBlock& declarations
															) const
{
	ExportGaussElim::getFunctionDeclarations( declarations );

	declarations.addDeclaration( solve_full );
	if( REUSE ) {
		declarations.addDeclaration( solveReuse_full );
	}

	return SUCCESSFUL_RETURN;
}


returnValue ExportIRK3StageSingleNewton::getCode(	ExportStatementBlock& code
											)
{
	if( fabs(tau) <= ZERO_EPS || transf1.isEmpty() || transf2.isEmpty() || fabs(stepsize) <= ZERO_EPS ) return ACADOERROR(RET_INVALID_OPTION);

	setupFactorization( solve, rk_swap, determinant, string("fabs") );
	code.addFunction( solve );

	if( REUSE ) { // Also export the extra function which reuses the factorization of the matrix A
		setupSolveReuseComplete( solveReuse, rk_bPerm );
		code.addFunction( solveReuse );
	}
	
	ExportVariable tau_var( stepsize*tau );

	// SETUP solve_full
	ExportIndex i( "i" );
	ExportIndex j( "j" );
	solve_full.addIndex(i);
	solve_full.addIndex(j);

	// form the linear subsystem matrix
	ExportForLoop loop01( i, 0, dim );
	ExportForLoop loop02( j, 0, dim );
	if( implicit ) {
		loop02.addStatement( A_mem.getElement(i,j) == tau_var*A_full.getElement(i,j) );
		loop02.addStatement( A_mem.getElement(i,j) += I_full.getElement(i,j) );
	}
	else {
		loop02.addStatement( A_mem.getElement(i,j) == tau_var*A_full.getElement(i,j) );
	}
	loop01.addStatement( loop02 );
	solve_full.addStatement( loop01 );
	if( !implicit ) {
		ExportForLoop loop1( i, 0, dim );
		loop1.addStatement( A_mem.getElement(i,i) -= 1.0 );
		solve_full.addStatement( loop1 );
	}

	// factorize the real and complex linear systems
	solve_full.addFunctionCall(getNameSubSolveFunction(),A_mem,rk_perm_full);

	code.addFunction( solve_full );

	// SETUP solveReuse_full
	if( REUSE ) {
		solveReuse_full.addIndex(i);
		solveReuse_full.addIndex(j);

		ExportVariable transf1_var( transf1 );
		ExportVariable transf2_var( transf2 );
		ExportVariable low_tria_var( low_tria );

		// transform the right-hand side
		performTransformation( solveReuse_full, b_full, b_mem, transf1_var, i );

		// solveReuse the real and complex linear systems
		solveReuse_full.addFunctionCall(getNameSubSolveReuseFunction(),A_mem,b_mem.getAddress(0,0),rk_perm_full);

		if( !implicit ) {
			ExportForLoop loop2( i, 0, dim );
			loop2.addStatement( b_mem.getRow(dim+i) -= low_tria_var.getElement(0,0)*b_mem.getRow(i) );
			solveReuse_full.addStatement( loop2 );
			solveReuse_full.addFunctionCall(getNameSubSolveReuseFunction(),A_mem,b_mem.getAddress(dim,0),rk_perm_full);

			ExportForLoop loop3( i, 0, dim );
			loop3.addStatement( b_mem.getRow(2*dim+i) -= low_tria_var.getElement(1,0)*b_mem.getRow(i) );
			solveReuse_full.addStatement( loop3 );
			ExportForLoop loop4( i, 0, dim );
			loop4.addStatement( b_mem.getRow(2*dim+i) -= low_tria_var.getElement(2,0)*b_mem.getRow(dim+i) );
			solveReuse_full.addStatement( loop4 );
			solveReuse_full.addFunctionCall(getNameSubSolveReuseFunction(),A_mem,b_mem.getAddress(2*dim,0),rk_perm_full);
		}
		else {
			uint k;
			ExportForLoop loop22( j, 0, dim );
			ExportForLoop loop2( i, 0, dim );
			for( k = 0; k < nRightHandSides; k++ ) {
				loop2.addStatement( b_mem.get(dim+j,k) + " += " + low_tria_var.get(0,0) + "*" + I_full.get(j,i) + "*" + b_mem.get(i,k) + ";\n" );
			}
			loop22.addStatement( loop2 );
			solveReuse_full.addStatement( loop22 );
			solveReuse_full.addFunctionCall(getNameSubSolveReuseFunction(),A_mem,b_mem.getAddress(dim,0),rk_perm_full);

			ExportForLoop loop33( j, 0, dim );
			ExportForLoop loop3( i, 0, dim );
			for( k = 0; k < nRightHandSides; k++ ) {
				loop3.addStatement( b_mem.get(2*dim+j,k) + " += " + low_tria_var.get(1,0) + "*" + I_full.get(j,i) + "*" + b_mem.get(i,k) + ";\n" );
			}
			loop33.addStatement( loop3 );
			solveReuse_full.addStatement( loop33 );

			ExportForLoop loop44( j, 0, dim );
			ExportForLoop loop4( i, 0, dim );
			for( k = 0; k < nRightHandSides; k++ ) {
				loop4.addStatement( b_mem.get(2*dim+j,k) + " += " + low_tria_var.get(2,0) + "*" + I_full.get(j,i) + "*" + b_mem.get(dim+i,k) + ";\n" );
			}
			loop44.addStatement( loop4 );
			solveReuse_full.addStatement( loop44 );
			solveReuse_full.addFunctionCall(getNameSubSolveReuseFunction(),A_mem,b_mem.getAddress(2*dim,0),rk_perm_full);
		}

		// transform back to the solution
		performTransformation( solveReuse_full, b_mem, b_full, transf2_var, i );

		code.addFunction( solveReuse_full );
	}

	return SUCCESSFUL_RETURN;
}


returnValue ExportIRK3StageSingleNewton::performTransformation(	ExportStatementBlock& code, const ExportVariable& from, const ExportVariable& to, const ExportVariable& transf, const ExportIndex& index )
{
	uint i, j;

	ExportForLoop loop0( index, 0, 3*dim );
	for( j = 0; j < nRightHandSides; j++ ) {
		loop0.addStatement( to.getElement(index,j) == 0.0 );
	}
	code.addStatement( loop0 );

	ExportForLoop loop1( index, 0, dim );
	for( j = 0; j < nRightHandSides; j++ ) {

		for( i = 0; i < 3; i++ ) {
			if( !transf.isZero(0,i) ) loop1.addStatement( to.getElement(index,j) += transf.getElement(0,i)*from.getElement(index+i*dim,j) );
		}

		for( i = 0; i < 3; i++ ) {
			if( !transf.isZero(1,i) ) loop1.addStatement( to.getElement(dim+index,j) += transf.getElement(1,i)*from.getElement(index+i*dim,j) );
		}

		for( i = 0; i < 3; i++ ) {
			if( !transf.isZero(2,i) ) loop1.addStatement( to.getElement(2*dim+index,j) += transf.getElement(2,i)*from.getElement(index+i*dim,j) );
		}
	}
	code.addStatement( loop1 );

	return SUCCESSFUL_RETURN;
}


returnValue ExportIRK3StageSingleNewton::setup( )
{
	ExportGaussElim::setup( );

	if (nRightHandSides <= 0)
		return ACADOERROR(RET_INVALID_OPTION);

	int useOMP;
	get(CG_USE_OPENMP, useOMP);
	ExportStruct structWspace;
	structWspace = useOMP ? ACADO_LOCAL : ACADO_WORKSPACE;

	A_mem = ExportVariable( std::string( "rk_mem_" ) + identifier + "A", dim, dim, REAL, structWspace );
	b_mem = ExportVariable( std::string( "rk_mem_" ) + identifier + "b", 3*dim, nRightHandSides, REAL, structWspace );

	solve = ExportFunction( getNameSubSolveFunction(), A, rk_perm );
	solve.setReturnValue( determinant, false );
	solve.addLinebreak( );	// FIX: TO MAKE SURE IT GETS EXPORTED
	
	if( REUSE ) {
		solveReuse = ExportFunction( getNameSubSolveReuseFunction(), A, b, rk_perm );
		solveReuse.addLinebreak( );	// FIX: TO MAKE SURE IT GETS EXPORTED
	}

	A_full = ExportVariable( "A", dim, dim, REAL );
	I_full = ExportVariable( "A_I", dim, dim, REAL );
	b_full = ExportVariable( "b", 3*dim, nRightHandSides, REAL );
	rk_perm_full = ExportVariable( "rk_perm", 1, dim, INT );

	if( implicit ) {
		solve_full = ExportFunction( getNameSolveFunction(), A_full, I_full, rk_perm_full );   // Only perform the LU factorization!
	}
	else {
		solve_full = ExportFunction( getNameSolveFunction(), A_full, rk_perm_full );   // Only perform the LU factorization!
	}
	solve_full.setReturnValue( determinant, false );
	solve_full.addLinebreak( );	// FIX: TO MAKE SURE IT GETS EXPORTED
	if( REUSE ) {
		if( implicit ) {
			solveReuse_full = ExportFunction( getNameSolveReuseFunction(), A_full, I_full, b_full, rk_perm_full );
		}
		else {
			solveReuse_full = ExportFunction( getNameSolveReuseFunction(), A_full, b_full, rk_perm_full );
		}
		solveReuse_full.addLinebreak( );	// FIX: TO MAKE SURE IT GETS EXPORTED
	}

    return SUCCESSFUL_RETURN;
}


returnValue ExportIRK3StageSingleNewton::setTransformations( const double _tau, const DVector& _low_tria, const DMatrix& _transf1, const DMatrix& _transf2 ) {
	tau = _tau;
	low_tria = _low_tria;
	transf1 = _transf1;
	transf2 = _transf2;

	if( _tau <= 0 ) return ACADOERROR( RET_INVALID_ARGUMENTS );
	if( _transf1.getNumRows() != 3 || _transf1.getNumCols() != 3 ) return ACADOERROR( RET_INVALID_ARGUMENTS );
	if( _transf2.getNumRows() != 3 || _transf2.getNumCols() != 3 ) return ACADOERROR( RET_INVALID_ARGUMENTS );
	if( _low_tria.getDim() != 3 ) return ACADOERROR( RET_INVALID_ARGUMENTS );
	if( _transf1.isZero() || _transf2.isZero() || _low_tria.isZero() ) return ACADOERROR( RET_INVALID_ARGUMENTS );

	return SUCCESSFUL_RETURN;
}


returnValue ExportIRK3StageSingleNewton::setStepSize( double _stepsize ) {
	stepsize = _stepsize;

	return SUCCESSFUL_RETURN;
}


const std::string ExportIRK3StageSingleNewton::getNameSubSolveFunction() {

	return string( "solve_" ) + identifier + "sub_system";
}


const std::string ExportIRK3StageSingleNewton::getNameSubSolveReuseFunction() {

	return string( "solve_" ) + identifier + "sub_system_reuse";
}

returnValue ExportIRK3StageSingleNewton::setImplicit( BooleanType _implicit ) {

	implicit = _implicit;

	return SUCCESSFUL_RETURN;
}


//
// PROTECTED MEMBER FUNCTIONS:
//



CLOSE_NAMESPACE_ACADO

// end of file.
